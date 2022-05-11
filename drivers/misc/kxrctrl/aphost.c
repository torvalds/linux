/*
 * SPI controller driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
 
#include "aphost.h"

#define A11B_NRF


#define CMD_DATA_TAG  0xA6
#define CMD_CLR_BOND_TAG  0xA7
#define CMD_REQUEST_TAG   (0xA8)
#define CMD_EXTDATA_RLEDTAG  0xB6

//#define  INTERFACE_ADD

#define YC_START_HOST		_IO('q', 1)
#define YC_STOP_HOST		_IO('q', 2)
#define YC_DATA_NOTIFY	    _IO('q',3)
#define YC_START_VIB		_IO('q', 4)
#define YC_STOP_VIB		    _IO('q', 5)


#define YC_GET_DATA 



struct jspinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *active;
	struct pinctrl_state *suspend;
};


struct js_spi_client {
	struct spi_device *spi_client;
	struct task_struct   *kthread;
	struct mutex js_mutex;    /* power mutex*/
	struct mutex js_sm_mutex; /*dma alloc and free mutex*/
	
	struct jspinctrl_info pinctrl_info;
	
	int js_lfen_gpio; /*level shift en gpio*/
	int js_irq_gpio;
	int js_rled_en_gpio;/*A11B used as all rled trig ,but v02a used as left rled */
	int js_tst2_gpio;   /*just old test gpio ,not used in a11b and v02a*/
	int js_dfu_en_gpio; /*dfu enable gpio ,low enable dfu */    
	int js_v02a_rled_right_en_gpio; /* A11B not used  , V02A useed for right rled */
	int js_v33en_gpio;    
	int js_ledl_gpio; /*old test ,not used now*/
	int js_ledr_gpio; /*old test ,not used now*/
	int js_irq;
	
	atomic_t dataflag;
	atomic_t rledchg;
	atomic_t userRequest; //request from userspace
	atomic_t nordicAcknowledge; //ack from nordic52832 master
	unsigned char JoyStickBondState; //1:left JoyStick 2:right JoyStick
	bool suspend;
	
	wait_queue_head_t  wait_queue;
	void	*vaddr;
	size_t vsize;
	struct dma_buf *js_buf;
    spinlock_t smem_lock;
    
	struct miscdevice miscdev;
	uint64_t tss;
	uint64_t ts_offset;

    unsigned char txbuffer[255];
    unsigned char rxbuffer[255];

	uint64_t tsHost; /*linux boottime */
	uint64_t tsoffset; /*time offset between two cpu*/
	uint64_t tsoffsetmono;/*linux monotime ,need by app */
	uint64_t tsSyncPt;
	uint64_t tsSyncPtmono;
    uint32_t tshmd_tmp;/*get the time from hmd*/
	unsigned char SyncPtFlag;
	unsigned char powerstate;
	bool     irqstate;
	unsigned char  js_lstate;
	unsigned char  js_rstate;

    struct hrtimer hr_timer; 
    ktime_t ktime;  

	struct usb_device *udev;
	struct usb_host_interface *desc;
	struct usb_endpoint_descriptor *endpoint;
    struct usb_interface *intf;
	struct urb *urb;
	unsigned int pipe;
	u8 ubuffer[128];
	struct work_struct work;

    int memfd;
	atomic_t urbstate;
};

struct js_spi_client *gspi_client = NULL;

cp_buffer_t *u_packet=NULL; 

static char checkoutpoint=0;


void d_packet_set_instance(cp_buffer_t *in )
{

    if(gspi_client==NULL){
        pr_err("js %s: drv init err", __func__);
    }

    spin_lock(&gspi_client->smem_lock);

    if(in==NULL){
        u_packet=NULL;        
    }
    else{
        u_packet=in;
        u_packet->c_head=-1;
        u_packet->p_head=-1;
    }
    
    spin_unlock(&gspi_client->smem_lock);
    
    if(in==NULL)
        pr_err("js %s:  release mem", __func__);
    else
        pr_err("js %s:  alloc mem", __func__);

}



void js_irq_enable(struct js_spi_client   *spi_client,bool enable)
{

    if(spi_client->irqstate==enable){
       pr_err("js irq already =%d ",enable);
       return;
    }
    
    pr_err("js irq en =%d ",enable);
	if(enable){
		enable_irq(spi_client->js_irq);
    }
	else{
		disable_irq(spi_client->js_irq);
    }
    
    spi_client->irqstate=enable;
	
}



void js_set_power(int jspower)
{
  if(gspi_client)
  {
        mutex_lock(&gspi_client->js_mutex);
        if(gspi_client->powerstate != jspower)
		{   
            if(jspower==0){/*off */
                   gspi_client->powerstate=0;
                   js_irq_enable(gspi_client,false);
                   gpio_set_value(gspi_client->js_dfu_en_gpio,1);
                   gpio_set_value(gspi_client->js_lfen_gpio,0);
                   gpio_set_value(gspi_client->js_v33en_gpio,0);
            }
            else if(jspower==1){ /*normal on*/
                   gpio_set_value(gspi_client->js_dfu_en_gpio,1);
                   gpio_set_value(gspi_client->js_v33en_gpio,1);
                   gpio_set_value(gspi_client->js_lfen_gpio,1);
                   gspi_client->powerstate=1;
                   js_irq_enable(gspi_client,true);
            }
           else if(jspower==2){/*dfu*/ 
               
               gspi_client->powerstate=2;
               js_irq_enable(gspi_client,false);
               gpio_set_value(gspi_client->js_dfu_en_gpio,0);
               gpio_set_value(gspi_client->js_v33en_gpio,1);
               gpio_set_value(gspi_client->js_lfen_gpio,1);
               msleep(100);
           }
       }
       mutex_unlock(&gspi_client->js_mutex);
    }
}

static ssize_t jspower_show(struct device *dev,struct device_attribute *attr, char *buf)
{

 	return sprintf(buf, "%d\n",(unsigned int)gspi_client->powerstate);
}


static ssize_t jspower_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)  
{
   int ctl=0;
   
   if(sscanf(buf,"%d",&ctl)==1)
   {	
		printk("[%s]set power:%d\n", __func__, ctl);
		if(gspi_client)
		{
            if(ctl==0){
                js_set_power(0);
            }
            else if(ctl==1){
                js_set_power(1);
            }
            else if(ctl==2){
                js_set_power(2);
            }
 
		}
   }
      
   return size;
}

static ssize_t jsmem_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", gspi_client->memfd);
}

static ssize_t jsmem_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	cp_buffer_t * inbuf;

	ret = kstrtoint(buf, 10, &gspi_client->memfd);
	if (ret < 0)
		return ret;

    mutex_lock(&gspi_client->js_sm_mutex);
	
	if (gspi_client->memfd == -1){
        
		if (IS_ERR_OR_NULL(gspi_client->vaddr))
			goto __end;
        
		d_packet_set_instance(NULL);
		dma_buf_kunmap(gspi_client->js_buf, 0, gspi_client->vaddr);
		dma_buf_end_cpu_access(gspi_client->js_buf, DMA_BIDIRECTIONAL);
		dma_buf_put(gspi_client->js_buf);
		gspi_client->vaddr = NULL;
		gspi_client->js_buf = NULL;
	}
	else
	{
		gspi_client->js_buf = dma_buf_get(gspi_client->memfd);
		if (IS_ERR_OR_NULL(gspi_client->js_buf)) {
			ret = -ENOMEM;
			pr_err("[%s]dma_buf_get failed for fd: %d\n", __func__, gspi_client->memfd);
			goto __end;
		}
		
		ret = dma_buf_begin_cpu_access(gspi_client->js_buf, DMA_BIDIRECTIONAL);
		if (ret) {
			pr_err("[%s]: dma_buf_begin_cpu_access failed\n", __func__);
			dma_buf_put(gspi_client->js_buf);
			gspi_client->js_buf = NULL;
			goto __end;
		}
		
		gspi_client->vsize = gspi_client->js_buf->size;
		gspi_client->vaddr = dma_buf_kmap(gspi_client->js_buf, 0);
		
		if (IS_ERR_OR_NULL(gspi_client->vaddr)) {
			
			dma_buf_end_cpu_access(gspi_client->js_buf, DMA_BIDIRECTIONAL);
			dma_buf_put(gspi_client->js_buf);
			gspi_client->js_buf = NULL;
			pr_err("[%s]dma_buf_kmap failed for fd: %d\n",__func__,  gspi_client->memfd);
			goto __end;
		}
		
        inbuf=(cp_buffer_t *)gspi_client->vaddr;
		d_packet_set_instance(inbuf);
	}

__end:
    mutex_unlock(&gspi_client->js_sm_mutex);

	return count;
}


static ssize_t jsoffset_show(struct device *dev,struct device_attribute *attr, char *buf)
{
 	return sprintf(buf, "%llu,%llu\n",gspi_client->tsoffset,gspi_client->tsoffsetmono);
}

static ssize_t jsrequest_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	unsigned int input = 0;
	acknowledge_t nordicAck;
	int size = 0;

	mutex_lock(&gspi_client->js_mutex);
	memset(&nordicAck, 0, sizeof(acknowledge_t));
	input = atomic_read(&gspi_client->nordicAcknowledge);
	atomic_set(&gspi_client->nordicAcknowledge, 0);
	nordicAck.acknowledgeHead.requestType = ((input&0x7f000000) >> 24);
	nordicAck.acknowledgeHead.ack = ((input&0x80000000) >> 31);
	nordicAck.acknowledgeData[0] = (input&0x000000ff);
	nordicAck.acknowledgeData[1] = ((input&0x0000ff00) >> 8);
	nordicAck.acknowledgeData[2] = ((input&0x00ff0000) >> 16);

	if (nordicAck.acknowledgeHead.ack == 1)
	{
		switch(nordicAck.acknowledgeHead.requestType)
		{
			case getMasterNordicVersionRequest:
				size = sprintf(buf, "masterNordic fwVersion:%d.%d\n", nordicAck.acknowledgeData[1], nordicAck.acknowledgeData[0]);
				break;
			case bondJoyStickRequest:
			case disconnectJoyStickRequest:
			case setVibStateRequest:
			case hostEnterDfuStateRequest:
				size = sprintf(buf, "requestType:%d ack:%d\n",nordicAck.acknowledgeHead.requestType, nordicAck.acknowledgeHead.ack);
				break;
			case getJoyStickBondStateRequest:
				gspi_client->JoyStickBondState = (nordicAck.acknowledgeData[0]&0x03);
				size = sprintf(buf, "left/right joyStick bond state:%d:%d\n", (gspi_client->JoyStickBondState&0x01), ((gspi_client->JoyStickBondState&0x02)>>1));
				break;
			case getLeftJoyStickProductNameRequest:
				size = sprintf(buf, "leftJoyStick productNameID:%d\n", nordicAck.acknowledgeData[0]);
				break;
			case getRightJoyStickProductNameRequest:
				size = sprintf(buf, "rightJoyStick productNameID:%d\n", nordicAck.acknowledgeData[0]);
				break;
			case getLeftJoyStickFwVersionRequest:
				size = sprintf(buf, "leftJoyStick fwVersion:%d.%d\n", nordicAck.acknowledgeData[1], nordicAck.acknowledgeData[0]);
				break;
			case getRightJoyStickFwVersionRequest:
				size = sprintf(buf, "rightJoyStick fwVersion:%d.%d\n", nordicAck.acknowledgeData[1], nordicAck.acknowledgeData[0]);
				break;
			default:
				size = sprintf(buf, "invalid requestType\n");
				break;
		}
	}
	else
	{	
		size = sprintf(buf, "no need to ack\n");
	}
	mutex_unlock(&gspi_client->js_mutex);
	return size;
}

static ssize_t jsrequest_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)  
{
  
   unsigned int input = 0;
   request_t request;
   int vibState = 0;
   
   mutex_lock(&gspi_client->js_mutex);
   if(sscanf(buf, "%x", &input) == 1)
   {		
		memset(&request, 0, sizeof(request_t));
		request.requestHead.requestType = ((input&0x7f000000) >> 24);
		request.requestData[0] = (input&0x000000ff);
		request.requestData[1] = (input&0x0000ff00);
		request.requestData[2] = (input&0x00ff0000);

		switch(request.requestHead.requestType)
		{
			case setVibStateRequest:
				vibState = ((request.requestData[1] << 8) | request.requestData[0]);
				if (vibState >= 0 && vibState <= 0xffff)
				{
					if(gspi_client) {
						atomic_set(&gspi_client->userRequest, input);
						atomic_inc(&gspi_client->dataflag);
						wake_up_interruptible(&gspi_client->wait_queue);
					}
				}
				else
				{
					printk("invalid vibState\n");
					memset(&gspi_client->userRequest, 0, sizeof(gspi_client->userRequest));
				}
				break;
			case getMasterNordicVersionRequest:
			case bondJoyStickRequest:
			case disconnectJoyStickRequest:
			case getJoyStickBondStateRequest:
			case hostEnterDfuStateRequest:
			case getLeftJoyStickProductNameRequest:
			case getRightJoyStickProductNameRequest:
			case getLeftJoyStickFwVersionRequest:
			case getRightJoyStickFwVersionRequest:
				if(gspi_client)
				{
					atomic_set(&gspi_client->userRequest, input);
					atomic_inc(&gspi_client->dataflag);
					wake_up_interruptible(&gspi_client->wait_queue);
				}
				break;
			default:
				printk("invalid requestType\n");
				memset(&gspi_client->userRequest, 0, sizeof(gspi_client->userRequest));
				return size;
		}		
   }
   mutex_unlock(&gspi_client->js_mutex);
   
   return size;
}

//static DEVICE_ATTR(jsbond, S_IRUGO|S_IWUSR|S_IWGRP, jsbond_show, jsbond_store);

static DEVICE_ATTR(jsmem, S_IRUGO|S_IWUSR|S_IWGRP, jsmem_show, jsmem_store);

static DEVICE_ATTR(jspower, S_IRUGO|S_IWUSR|S_IWGRP, jspower_show, jspower_store); /*external  for power ctl ,avold someone  */

static DEVICE_ATTR(jsoffset, S_IRUGO, jsoffset_show, NULL);

static DEVICE_ATTR(jsrequest, S_IRUGO|S_IWUSR|S_IWGRP, jsrequest_show, jsrequest_store);


static int js_spi_txfr(struct spi_device *spi, char *txbuf,char *rxbuf, int num_byte,uint64_t *tts)
{

	int ret=0;
	

	struct spi_transfer txfr;
	struct spi_message msg;

	memset(&txfr, 0, sizeof(txfr));
	txfr.tx_buf = txbuf;
	txfr.rx_buf = rxbuf;
	txfr.len = num_byte;
	spi_message_init(&msg);
	spi_message_add_tail(&txfr, &msg);
	
	*tts=ktime_to_ns(ktime_get_boottime());
	ret=spi_sync(spi, &msg);
	
    if(ret<0)
   	{
		pr_err(" js xfr err=%d \n",ret);
   	}
	
	return ret;
}



#define XFR_SIZE  188

int js_thread(void *data)
{
	int ret;
	unsigned char *pbuf;
	uint64_t tts;
	uint32_t tth[8];
	uint64_t tto[8];
    int num = 0;
    int pksz = 0;
	int index = 0;
	uint32_t hosttime;
	bool skiprport = false;
	unsigned int input = 0;
    request_t currentRequest;
	static request_t lastRequest;
	acknowledge_t nordicAck;
	uint8_t val = 0;
	struct js_spi_client  *spi_client=(struct js_spi_client *)data;
	
	struct sched_param param = {
		.sched_priority = 88
	};
	
	sched_setscheduler(current, SCHED_RR, &param);
	//set_current_state(TASK_INTERRUPTIBLE);		

	pr_err(" js_thread start \n");

	do {
		skiprport = false;
		ret = wait_event_interruptible(spi_client->wait_queue, atomic_read(&spi_client->dataflag) || kthread_should_stop());
	
		if ((ret < 0) || kthread_should_stop()) {
			pr_err("%s: exit\n", __func__);
			break;
		}
        atomic_set(&spi_client->dataflag, 0);

        if(spi_client->powerstate != 1){
            msleep(100);
			continue;
        }

		input = (unsigned int)atomic_read(&gspi_client->userRequest);
		
		val = gpio_get_value(spi_client->js_irq_gpio);
	    if(val == 0 && input == 0) //Filter out the exception trigger
		{
			continue;
	    }
	
		memset(&currentRequest, 0, sizeof(request_t));
		currentRequest.requestHead.needAck = ((input&0x80000000) >> 31);
		currentRequest.requestHead.requestType = ((input&0x7f000000) >> 24);
		currentRequest.requestData[0] = (input&0x000000ff);
		currentRequest.requestData[1] = ((input&0x0000ff00) >> 8);
		currentRequest.requestData[2] = ((input&0x00ff0000) >> 16);
		
		memset(spi_client->txbuffer, 0, sizeof(spi_client->txbuffer));
		memset(spi_client->rxbuffer, 0, sizeof(spi_client->rxbuffer));
		spi_client->txbuffer[0] = CMD_REQUEST_TAG;
		spi_client->txbuffer[1] = ((currentRequest.requestHead.needAck << 7)|currentRequest.requestHead.requestType);
		
		
		switch(currentRequest.requestHead.requestType)
		{
			case setVibStateRequest:
				spi_client->txbuffer[2] = currentRequest.requestData[0];
				spi_client->txbuffer[3] = currentRequest.requestData[1];
				break;
			case bondJoyStickRequest:
			case disconnectJoyStickRequest:
				spi_client->txbuffer[2] = (currentRequest.requestData[0]&0x01);
				break;
			default:
				break;
		}
        if(spi_client->powerstate == 1)
		{
		    ret = js_spi_txfr(spi_client->spi_client, spi_client->txbuffer, spi_client->rxbuffer, XFR_SIZE, &tts);
			if (ret != 0)
				continue;
        }
        else
		{
			continue;
        }

		if (spi_client->rxbuffer[4] == 0xff) //Filtering dirty Data
		{
			continue;
		}
		
		if(lastRequest.requestHead.needAck == 1)
		{
			memset(&nordicAck, 0, sizeof(acknowledge_t));
			nordicAck.acknowledgeHead.ack = ((spi_client->rxbuffer[0]&0x80)>>7);
			nordicAck.acknowledgeHead.requestType = (spi_client->rxbuffer[0]&0x7f);
			nordicAck.acknowledgeData[0] = spi_client->rxbuffer[1];
			nordicAck.acknowledgeData[1] = spi_client->rxbuffer[2];
			nordicAck.acknowledgeData[2] = spi_client->rxbuffer[3];
			if (lastRequest.requestHead.requestType == nordicAck.acknowledgeHead.requestType)
			{
				unsigned int input = 0;

				input = ((spi_client->rxbuffer[0]<<24)|(spi_client->rxbuffer[3]<<16)|(spi_client->rxbuffer[2]<<8)|spi_client->rxbuffer[1]);
				atomic_set(&spi_client->nordicAcknowledge, input);
			}
			memset(&lastRequest, 0, sizeof(lastRequest));
		}
		
		if ((gspi_client->JoyStickBondState&0x03) != 0 && input == 0) //left or right joyStick are bound
		//if ((gspi_client->JoyStickBondState&0x03) != 0) //left or right joyStick are bound
		{
			pksz = spi_client->rxbuffer[4];
			num = spi_client->rxbuffer[5];
			
			if(num == 0 || pksz != 30)
			{
				//pr_err("wjx no joystick data\n");
				skiprport = true;
			}
			memcpy(&hosttime, &spi_client->rxbuffer[6], 4);
			tts = spi_client->tsHost;
			
			pbuf = &spi_client->rxbuffer[10];
	        if(!skiprport){
	            /*add Protection if someone release the memory  */          
	            spin_lock(&gspi_client->smem_lock);
				
		        for(index = 0; index < num; index++)
		        {
				    memcpy(&tth[index], pbuf, 4);
					tto[index] = tts-(hosttime-tth[index])*100000;
					if((u_packet)&&(spi_client->vaddr))
					{
						int8_t p_head;
						d_packet_t *pdata;

						p_head = (u_packet->p_head + 1) % MAX_PACK_SIZE;
						pdata = &u_packet->data[p_head];
						pdata->ts = tto[index];
						pdata->size = pksz - 4;
						memcpy((void*)pdata->data, (void*)(pbuf+4), pksz-4);
						u_packet->p_head = p_head;		
		            }						
					pbuf += pksz;
				}
	            spin_unlock(&gspi_client->smem_lock);
	        }
		}

		if (currentRequest.requestHead.requestType != 0)
			atomic_set(&gspi_client->userRequest, 0);

		memcpy(&lastRequest, &currentRequest, sizeof(currentRequest));		
	} while (1);

	return 0;
}


static int js_pinctrl_init(struct js_spi_client   *spi_client)
{
	int rc = 0;

	spi_client->pinctrl_info.pinctrl= devm_pinctrl_get(&spi_client->spi_client->dev);
	
	if (IS_ERR_OR_NULL(spi_client->pinctrl_info.pinctrl)) {
		rc = PTR_ERR(spi_client->pinctrl_info.pinctrl);
		pr_err("failed  pinctrl, rc=%d\n", rc);
		goto error;
	}

	spi_client->pinctrl_info.active = pinctrl_lookup_state(spi_client->pinctrl_info.pinctrl, "js_default");
	if (IS_ERR_OR_NULL(spi_client->pinctrl_info.active)) {
		rc = PTR_ERR(spi_client->pinctrl_info.active);
		pr_err("failed  pinctrl active state, rc=%d\n", rc);
		goto error;
	}

	spi_client->pinctrl_info.suspend =pinctrl_lookup_state(spi_client->pinctrl_info.pinctrl, "js_sleep");

	if (IS_ERR_OR_NULL(spi_client->pinctrl_info.suspend)) {
		rc = PTR_ERR(spi_client->pinctrl_info.suspend);
		pr_err("failed  pinctrl suspend state, rc=%d\n", rc);
		goto error;
	}
	pr_err("js_pinctrl_init ok \n");

error:
	return rc;
}


static int js_parse_gpios(struct js_spi_client   *spi_client)
{
	int rc = 0;

	struct device_node *of_node = spi_client->spi_client->dev.of_node;

	spi_client->js_lfen_gpio= of_get_named_gpio(of_node,"js,lfen-gpio", 0);
	if (!gpio_is_valid(spi_client->js_lfen_gpio)) {
		pr_err("failed get   js_lfen_gpio gpio, rc=%d\n", rc);
		rc = -EINVAL;
		goto error;
	}

#ifdef A11B_NRF
	spi_client->js_v33en_gpio= of_get_named_gpio(of_node,"js,v33en-gpio", 0);
	if (!gpio_is_valid(spi_client->js_v33en_gpio)) {
		pr_err("failed get   js_v33en_gpio gpio, rc=%d\n", rc);
		rc = -EINVAL;
		goto error;
	}
#endif

	spi_client->js_irq_gpio= of_get_named_gpio(of_node,"js,irq-gpio", 0);
	if (!gpio_is_valid(spi_client->js_irq_gpio)) {
		pr_err("failed get   js_irq_gpio gpio, rc=%d\n", rc);
		rc = -EINVAL;
		goto error;
	}

/*not used now*/
    spi_client->js_ledl_gpio= of_get_named_gpio(of_node,"js,ledl", 0);
    if (!gpio_is_valid(spi_client->js_ledl_gpio)) {
        pr_err("failed get   js_ledl_gpio gpio, rc=%d\n", rc);
    }

    spi_client->js_ledr_gpio= of_get_named_gpio(of_node,"js,ledr", 0);
    if (!gpio_is_valid(spi_client->js_ledr_gpio)) {
        pr_err("failed get   js_ledr_gpio gpio, rc=%d\n", rc);
    }



	spi_client->js_rled_en_gpio= of_get_named_gpio(of_node,"js,tst1", 0);
	if (!gpio_is_valid(spi_client->js_rled_en_gpio)) {
		pr_err("failed get	 js_rled_en_gpio gpio, rc=%d\n", rc);
		rc = -EINVAL;
		goto error;
	}

	spi_client->js_tst2_gpio= of_get_named_gpio(of_node,"js,tst2", 0);
	if (!gpio_is_valid(spi_client->js_tst2_gpio)) {
		pr_err("failed get	 js_tst2_gpio gpio, rc=%d\n", rc);
	}

	spi_client->js_dfu_en_gpio= of_get_named_gpio(of_node,"js,tst3", 0);
	if (!gpio_is_valid(spi_client->js_dfu_en_gpio)) {
		pr_err("failed get	 js_dfu_en_gpio gpio, rc=%d\n", rc);
	}

	spi_client->js_v02a_rled_right_en_gpio= of_get_named_gpio(of_node,"js,tst4", 0);
	if (!gpio_is_valid(spi_client->js_v02a_rled_right_en_gpio)) {
		pr_err("failed get	 js_v02a_rled_right_en_gpio gpio, rc=%d\n", rc);
	}

//tst


pr_err("js_parse_gpios ok \n");


error:
	return rc;
}



static int js_gpio_request(struct js_spi_client   *spi_client)
{
	int rc = 0;

	if (gpio_is_valid(spi_client->js_lfen_gpio)) {
		
		pr_err("request for js_lfen_gpio  =%d ", spi_client->js_lfen_gpio);
		rc = gpio_request(spi_client->js_lfen_gpio, "js_lfen_gpio");
		if (rc) {
			pr_err("request for js_lfen_gpio failed, rc=%d\n", rc);
			goto error;
		}
	}
	

#ifdef A11B_NRF
    if (gpio_is_valid(spi_client->js_v33en_gpio)) {
        
        pr_err("request for js_v33en_gpio  =%d ", spi_client->js_v33en_gpio);
        rc = gpio_request(spi_client->js_v33en_gpio, "js_v33en_gpio");
        if (rc) {
            pr_err("request for js_v33en_gpio failed, rc=%d\n", rc);
            goto error;
        }
    }
#endif

	if (gpio_is_valid(spi_client->js_irq_gpio)) {
		
		pr_err("request for js_irq_gpio  =%d ", spi_client->js_irq_gpio);
		rc = gpio_request(spi_client->js_irq_gpio, "js_irq_gpio");
		if (rc) {
			pr_err("request for js_irq_gpio failed, rc=%d\n", rc);
			goto error;
		}
	}


	if (gpio_is_valid(spi_client->js_ledl_gpio)) {
		
		pr_err("request for js_ledl_gpio  =%d ", spi_client->js_ledl_gpio);
		rc = gpio_request(spi_client->js_ledl_gpio, "js_ledl_gpio");
		if (rc) {
			pr_err("request for js_ledl_gpio failed, rc=%d\n", rc);
		}
        else
            gpio_direction_output(spi_client->js_ledl_gpio,1);
            
	}

	if (gpio_is_valid(spi_client->js_ledr_gpio)) {
		
		pr_err("request for js_ledr_gpio  =%d ", spi_client->js_ledr_gpio);
		rc = gpio_request(spi_client->js_ledr_gpio, "js_ledr_gpio");
		if (rc) {
			pr_err("request for js_ledr_gpio failed, rc=%d\n", rc);
		}
        else
            gpio_direction_output(spi_client->js_ledr_gpio,1);
            
	}


   
	if (gpio_is_valid(spi_client->js_rled_en_gpio)) {
		
		pr_err("request for js_rled_en_gpio  =%d ", spi_client->js_rled_en_gpio);
		rc = gpio_request(spi_client->js_rled_en_gpio, "js_rled_en_gpio");
		if (rc) {
			pr_err("request for js_rled_en_gpio failed, rc=%d\n", rc);
			goto error;
		}
		
        gpio_direction_output(spi_client->js_rled_en_gpio,0);

	}


	if (gpio_is_valid(spi_client->js_tst2_gpio)) {
		
		pr_err("request for js_tst2_gpio  =%d ", spi_client->js_tst2_gpio);
		rc = gpio_request(spi_client->js_tst2_gpio, "js_tst2_gpio");
		if (rc) {
			pr_err("request for js_tst2_gpio failed, rc=%d\n", rc);
		}
		else
			gpio_direction_input(spi_client->js_tst2_gpio);
	}

	if (gpio_is_valid(spi_client->js_dfu_en_gpio)) {
		
		pr_err("request for js_dfu_en_gpio  =%d ", spi_client->js_dfu_en_gpio);
		rc = gpio_request(spi_client->js_dfu_en_gpio, "js_dfu_en_gpio");
		if (rc) {
			pr_err("request for js_dfu_en_gpio failed, rc=%d\n", rc);
		}
		else
            gpio_direction_output(spi_client->js_dfu_en_gpio,0);
		
	}

	if (gpio_is_valid(spi_client->js_v02a_rled_right_en_gpio)) {
		
		pr_err("request for js_v02a_rled_right_en_gpio  =%d ", spi_client->js_v02a_rled_right_en_gpio);
		rc = gpio_request(spi_client->js_v02a_rled_right_en_gpio, "js_v02a_rled_right_en_gpio");
		if (rc) {
			pr_err("request for js_v02a_rled_right_en_gpio failed, rc=%d\n", rc);
		}
		else
            gpio_direction_output(spi_client->js_v02a_rled_right_en_gpio,0);
			//gpio_direction_input(spi_client->js_v02a_rled_right_en_gpio);
		
	}



    pr_err("js_gpio_request ok \n");

error:
	return rc;
}

static irqreturn_t  js_irq_handler(int irq, void *dev_id)
{
    int val = 0;
	struct js_spi_client *spi_client = (struct js_spi_client *)dev_id;

    if(spi_client->powerstate==1)
    {
    	val = gpio_get_value(spi_client->js_irq_gpio);
    	if(val == 1)
    	{
    		//disable_irq_nosync(spi_client->js_irq);
            spi_client->tsHost=ktime_to_ns(ktime_get_boottime());
    		atomic_inc(&spi_client->dataflag);
            wake_up_interruptible(&spi_client->wait_queue);
    	}
    }
	return IRQ_HANDLED;
}


static int js_io_init(struct js_spi_client   *spi_client)
{
	int ret;
		
	int rc = 0;
	
	rc=pinctrl_select_state(spi_client->pinctrl_info.pinctrl ,spi_client->pinctrl_info.active);
	if (rc)
       pr_err("js failed to set pin state, rc=%d\n",rc);


    gpio_direction_output(spi_client->js_dfu_en_gpio,1);
    gpio_direction_output(spi_client->js_v02a_rled_right_en_gpio,0);

    gpio_direction_input(spi_client->js_irq_gpio);
    gpio_direction_output(spi_client->js_lfen_gpio,0);

#ifdef A11B_NRF
    gpio_direction_output(spi_client->js_v33en_gpio,0);
#endif

	gpio_direction_output(spi_client->js_lfen_gpio,0);

    spi_client->powerstate=0;
    spi_client->js_lstate=1;
    spi_client->js_rstate=1;

	spi_client->js_irq = gpio_to_irq(spi_client->js_irq_gpio);
	
	if (spi_client->js_irq < 0) {
		spi_client->js_irq=-1;
		pr_err(" js  gpio_to_irq err\n");
	}
	else{
		 ret = request_irq(spi_client->js_irq, js_irq_handler,IRQF_TRIGGER_RISING, "js", spi_client);//IRQF_TRIGGER_FALLING 
		 disable_irq_nosync(spi_client->js_irq);
		 if(ret<0)
			pr_err("js request_irq err  =%d  \n",spi_client->js_irq);
          else
            pr_err("js request_irq =%d\n",spi_client->js_irq);
	}
	
	pr_err(" js_io_init ok\n");
	
	return 0;
}


/*
note:
this fuction used for :
1    notify to JoyStick the trig rled 
2 .  sync offset for android user space used to synctime .
*/         
static void glass_private_ep_callback(struct urb *urb)
{
     
     struct js_spi_client *client;

	 client = urb->context;

     if(urb->status==0) {
        
         if(gspi_client->js_lstate)
            gpio_set_value(client->js_rled_en_gpio,1);
         
         if(gspi_client->js_rstate)
            gpio_set_value(client->js_v02a_rled_right_en_gpio,1);

         if(checkoutpoint==0){
            gspi_client->tsSyncPt=ktime_to_ns(ktime_get_boottime());
            gspi_client->tsSyncPtmono=ktime_to_ns(ktime_get());
            
            memcpy(&(gspi_client->tshmd_tmp),(void *)(client->ubuffer),4);
            gspi_client->SyncPtFlag=1;
         }

         hrtimer_start( &client->hr_timer, client->ktime, HRTIMER_MODE_REL );
     }
     else
        pr_err("js notify_pri_callback err\n");
     
	schedule_work(&client->work);

}



enum hrtimer_restart timecallback( struct hrtimer *timer )  
{

   struct js_spi_client   *spi_client;
   spi_client = container_of(timer, struct js_spi_client ,hr_timer);

    gpio_set_value(spi_client->js_rled_en_gpio,0);
    gpio_set_value(spi_client->js_v02a_rled_right_en_gpio,0);


    return HRTIMER_NORESTART;  
}  


static int glass_private_chanel_probe(struct usb_interface *intf,const struct usb_device_id *id)
{
	int ret =0;

    struct js_spi_client *client;
    int maxpacket;

    if(gspi_client==NULL)
        return -ENOMEM;
    
    client =gspi_client;

	client->udev = interface_to_usbdev(intf);
    
	client->desc = intf->cur_altsetting;

	if (client->desc->desc.bNumEndpoints != 1){
        
        pr_err("js bNumEndpoints err \n");
	    return -1;
    }

    client->endpoint= &client->desc->endpoint[0].desc;

	if (!usb_endpoint_is_int_in( client->endpoint)){
        
        pr_err("js not ep \n");
        
        return -1;
    }


        
	usb_set_intfdata(intf, client);

    
	client->intf = intf;


	client->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!client->urb){
		return -ENOMEM;
     }

	client->pipe = usb_rcvintpipe(client->udev, client->endpoint->bEndpointAddress);

    
	maxpacket = usb_maxpacket(client->udev, client->pipe, usb_pipeout(client->pipe));

	usb_fill_int_urb(client->urb, client->udev, client->pipe, client->ubuffer, maxpacket,glass_private_ep_callback, client, client->endpoint->bInterval);

    atomic_set(&client->urbstate, 1);

	ret = usb_submit_urb(client->urb, GFP_KERNEL);
	if (ret < 0) {
        pr_err("js usb_submit_urb err =%d \n",ret);
        usb_free_urb(client->urb);
        return  ret ;
	}

    client->tsoffset=0;
    client->tsoffsetmono=0;
    checkoutpoint=0;

	return ret;


}


static void glass_private_chanel_disconnect(struct usb_interface *intf)
{
    struct js_spi_client *client;
    
	client = usb_get_intfdata(intf);
    
    client->tsoffset=0;
    client->tsoffsetmono=0;
    
    atomic_set(&client->urbstate, 0);

	usb_poison_urb(client->urb); 
    
	usb_free_urb(client->urb);
    
    checkoutpoint=0;
}



static int glass_private_chanel_ioctl(struct usb_interface *intf, unsigned int code, void *user_data)
{

// todo if need 
		return -ENOSYS;
}

static const struct usb_device_id yc_id_table[] = {
    { .match_flags = USB_DEVICE_ID_MATCH_DEVICE|USB_DEVICE_ID_MATCH_INT_CLASS,
      .idVendor  = 0x045e,
      .idProduct = 0x0659,
      .bInterfaceClass = 0xfe},
      
    { }	
};

MODULE_DEVICE_TABLE(usb, hub_id_table);


static struct usb_driver pri_driver = {
	.name =		"yc",
	.probe =	glass_private_chanel_probe,
	.disconnect =	glass_private_chanel_disconnect,
	.unlocked_ioctl = glass_private_chanel_ioctl,
	.id_table =	yc_id_table,
};



/*note : used to calculate  time offset for app use
move from irq to work for irq perfermance .
*/
static void ts_offset_update_event(struct work_struct *pwork)
{
	struct js_spi_client *client = container_of(pwork, struct js_spi_client, work);


    int  check= atomic_read(&client->urbstate);

    if(check==0) return;

    
    usb_submit_urb(client->urb, GFP_KERNEL);

    if(gspi_client->SyncPtFlag==1){
        
        gspi_client->SyncPtFlag=0;
        
        gspi_client->tsoffset = gspi_client->tsSyncPt-300000 - (uint64_t)(gspi_client->tshmd_tmp)*1000000;
        gspi_client->tsoffsetmono = gspi_client->tsSyncPtmono-300000 - (uint64_t)(gspi_client->tshmd_tmp)*1000000;
        //pr_err("js: offset:=%llu adr =%llu, hmdts=%d\n",gspi_client->tsoffset,gspi_client->tsSyncPt,gspi_client->tshmd_tmp);
    }

    checkoutpoint++;
    if(checkoutpoint==30)
        checkoutpoint=0;
   
}

static int js_spi_setup(struct spi_device *spi)
{
	struct js_spi_client   *spi_client;
	int                 rc = 0;


    pr_err("js js_spi_setup 1 \n");

	if((spi->dev.of_node)==NULL){
		
	   pr_err("js failed to check of_node \n");
	   return -ENOMEM;
	   
	}
    pr_err("js js_spi_setup 2 \n");

	spi_client = kzalloc(sizeof(*spi_client), GFP_KERNEL);
	if (!spi_client) {
        
        pr_err("js failed to malloc \n");
		return -ENOMEM;
	}


	
    pr_err("js js_spi_setup 3 \n");

	spi_client->spi_client = spi;

	rc=js_parse_gpios(spi_client);
   	if (rc) {
		pr_err("js failed to parse gpio, rc=%d\n", rc);
		goto spi_free;
	}


    rc =js_pinctrl_init(spi_client);
   	if (rc) {
		pr_err("js failed to init pinctrl, rc=%d\n", rc);
		goto spi_free;
	}

	rc = js_gpio_request(spi_client);
	if (rc) {
		pr_err("js failed to request gpios, rc=%d\n",rc);
		goto spi_free;
	}
    
	atomic_set(&spi_client->dataflag, 0);
    atomic_set(&spi_client->userRequest, 0);
	atomic_set(&spi_client->nordicAcknowledge, 0);
	
	mutex_init(&(spi_client->js_mutex));
	mutex_init(&(spi_client->js_sm_mutex));
	spin_lock_init(&spi_client->smem_lock);
	init_waitqueue_head(&spi_client->wait_queue);
    dev_set_drvdata(&spi->dev, spi_client);
    
    device_create_file(&spi->dev, &dev_attr_jsmem);
    device_create_file(&spi->dev, &dev_attr_jspower); 
    device_create_file(&spi->dev, &dev_attr_jsoffset); 
	device_create_file(&spi->dev, &dev_attr_jsrequest); 
    
	spi_client->suspend=false;
	spi_client->vaddr =NULL;
    spi_client->tsoffset=0;
    spi_client->tsoffsetmono=0;
	
	gspi_client = spi_client;

	spi_client->kthread =kthread_run(js_thread, spi_client, "jsthread");
	if (IS_ERR(spi_client->kthread))
		pr_err("js kernel_thread failed\r\n" );

    js_io_init(spi_client);
    
    spi_client->ktime = ktime_set(0, 200000);  
    hrtimer_init( &spi_client->hr_timer, CLOCK_BOOTTIME, HRTIMER_MODE_REL);  
    spi_client->hr_timer.function = timecallback; 
	
	INIT_WORK(&spi_client->work, ts_offset_update_event); 
	
    atomic_set(&spi_client->urbstate, 0);
    usb_register(&pri_driver);
	
	js_set_power(1);
	return rc;
spi_free:
	kfree(spi_client);
	return rc;
}



static int js_spi_suspend(struct device *dev)
{


	struct js_spi_client   *spi_client;
	
	if (!dev)
		return -EINVAL;

	spi_client = dev_get_drvdata(dev);
	if (!spi_client)
		return -EINVAL;

	spi_client->suspend=true ;
    js_set_power(0);
	pr_err("js_spi_suspend\n");

	return 0;


}


/* v02a  called by external module to trig the joystick rled */
void external_ctl_gpio(u8 mask )
{
   if(gspi_client) 
   {
       if (gpio_is_valid(gspi_client->js_rled_en_gpio)) 
       {
            if(mask&0x01)
                gpio_set_value(gspi_client->js_rled_en_gpio,1);
            else
                gpio_set_value(gspi_client->js_rled_en_gpio,0);
       }
       
       if (gpio_is_valid(gspi_client->js_v02a_rled_right_en_gpio)) 
       {
             if(mask&0x02)
                gpio_set_value(gspi_client->js_v02a_rled_right_en_gpio,1);
             else
                gpio_set_value(gspi_client->js_v02a_rled_right_en_gpio,0);
       }
    }
}



static int js_spi_resume(struct device *dev)
{
	struct js_spi_client   *spi_client;
	
	if (!dev)
		return -EINVAL;

	spi_client = dev_get_drvdata(dev);
	if (!spi_client)
		return -EINVAL;

    js_set_power(1);
	spi_client->suspend=false;    
	pr_err("[%s] exit\n", __func__);
	return 0;
}



static int js_spi_driver_probe(struct spi_device *spi)
{
    int ret;

    pr_err("js_spi_driver_probe start");

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	
    spi->max_speed_hz   = 8*1000*1000;

	ret=spi_setup(spi);
    
	if (ret < 0){
        pr_err("js spi_setup failed ret=%d",ret);
		return ret;
    }
    
    pr_err("js_spi_driver_probe ok");

	return js_spi_setup(spi);
}




static int js_spi_driver_remove(struct spi_device *sdev)
{
	return 0;
}




static const struct of_device_id js_dt_match[] = {
	{ .compatible = "yc,js" },
	{ }
};


static const struct dev_pm_ops js_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(js_spi_suspend, js_spi_resume)
};


static struct spi_driver js_spi_driver = {
	.driver = {
		.name = "yc,js",
		.owner = THIS_MODULE,
		.of_match_table = js_dt_match,
		.pm     = &js_pm_ops,
	},
	.probe = js_spi_driver_probe,
	.remove = js_spi_driver_remove,
	//.suspend			= js_spi_suspend,
	//.resume 			= js_spi_resume,
	
};


static int __init js_driver_init(void)
{
	int rc = 0;

    pr_err("js_driver_init");
    
	rc = spi_register_driver(&js_spi_driver);
	if (rc < 0) {
		pr_err("spi_register_driver failed rc = %d", rc);
		return rc;
	}

	return rc;
}

static void __exit js_driver_exit(void)
{
	spi_unregister_driver(&js_spi_driver);
}


module_init(js_driver_init); //late_initcall
module_exit(js_driver_exit);
MODULE_DESCRIPTION("joystick nordic52832 driver");
MODULE_LICENSE("GPL v2");
