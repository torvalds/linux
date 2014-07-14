/*
 * Copyright (c) 2010 -2013 Espressif System.
 *
 *   sdio serial i/f driver
 *    - sdio device control routines
 *    - sync/async DMA/PIO read/write
 *
 */
#ifdef ESP_USE_SPI

#include <linux/module.h>
#include <net/mac80211.h>
#include <linux/time.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>


#include "esp_pub.h"
#include "esp_sif.h"
#include "esp_sip.h"
#include "esp_debug.h"
#include "slc_host_register.h"
#include "esp_version.h"
#include "esp_ctrl.h"
#ifdef ANDROID
#include "esp_android.h"
#endif /* ANDROID */
#ifdef USE_EXT_GPIO
#include "esp_ext.h"
#endif /* USE_EXT_GPIO */

static int __init esp_spi_init(void);
static void __exit esp_spi_exit(void);

#define SPI_BLOCK_SIZE              (512)

#define MAX_BUF_SIZE        (48*1024)

static unsigned char *buf_addr = NULL;
static unsigned char *tx_cmd;
static unsigned char *rx_cmd;

struct task_struct *sif_irq_thread;

#define ESP_DMA_IBUFSZ   2048

//unsigned int esp_msg_level = 0;
unsigned int esp_msg_level = ESP_DBG_ERROR | ESP_SHOW;

static struct semaphore esp_powerup_sem;

static enum esp_sdio_state sif_sdio_state;
struct esp_spi_ctrl *sif_sctrl = NULL;

#ifdef ESP_ANDROID_LOGGER
bool log_off = false;
#endif /* ESP_ANDROID_LOGGER */

struct sif_req * sif_alloc_req(struct esp_spi_ctrl *sctrl);

#include "spi_stub.c"

void sif_lock_bus(struct esp_pub *epub)
{
        EPUB_FUNC_CHECK(epub);

        spi_bus_lock(EPUB_TO_FUNC(epub)->master);
}

void sif_unlock_bus(struct esp_pub *epub)
{
        EPUB_FUNC_CHECK(epub);

        spi_bus_unlock(EPUB_TO_FUNC(epub)->master);
}

int sif_spi_write_then_read(struct spi_device *spi, unsigned char* bufwrite, int sizesend, unsigned char* bufread, int sizeread)
{
	int error;

        error = spi_write_then_read(spi, bufwrite,sizesend, bufread, sizeread);
    
	if (error) {
		esp_dbg(ESP_DBG_ERROR, "%s: failed, error: %d\n",
			__func__, error);
		return error;
	}

	return 0;
}


int sif_spi_write_async_read(struct spi_device *spi, unsigned char* bufwrite,unsigned char* bufread,int size)
{
	struct spi_transfer xfer = {
              .rx_buf		= bufread,
		.tx_buf		= bufwrite,
		.len		= size,
		.bits_per_word	= 8,
		.speed_hz	= SPI_FREQ,
	};
	struct spi_message msg;
	int error;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	error = spi_sync_locked(spi, &msg);
	if (error) {
		esp_dbg(ESP_DBG_ERROR, "%s: failed, error: %d\n",
			__func__, error);
		return error;
	}

	return 0;
}

int sif_spi_write_raw(struct spi_device *spi, unsigned char* buf, int size)
{
	int err;
	struct spi_transfer xfer = {
		.tx_buf		= buf,
		.len		= size,
		.bits_per_word	= 8,
		.speed_hz	= SPI_FREQ,
	};
	struct spi_message msg;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	
	err = spi_sync_locked(spi, &msg);

	if (err) {
		esp_dbg(ESP_DBG_ERROR, "%s: failed, error: %d\n",
			__func__, err);
		return err;
	}

	return 0;
}

int sif_spi_read_raw(struct spi_device *spi, unsigned char* buf, int size)
{
        memset(buf,0xff,size);
        return sif_spi_write_async_read(spi,buf,buf,size);
}

int sif_spi_write_bytes(struct spi_device *spi, unsigned int addr, unsigned char *src,int count, int check_idle)
{
	int i;
        int pos,len;       
        unsigned char *tx_data = (unsigned char*)src;
        int err_ret = 0;     

	u32 timeout = 25000;
	u32 busy_state = 0x00;
       
	ASSERT(spi != NULL);
              
	if (check_idle)	{
		timeout = 25000;
		do {
			busy_state = 0x00;
			sif_spi_read_raw(spi, (u8 *)&busy_state, 4);
            
                    if(busy_state != 0xffffffff)
                            esp_dbg(ESP_DBG_ERROR, "%s busy_state:%x\n", __func__, busy_state); 
                        
		} while (busy_state != 0xffffffff && --timeout > 0);
	}

	if (timeout < 24000)
		esp_dbg(ESP_DBG_ERROR, "%s timeout[%d]\n", __func__, timeout); 


         if(count > 512 )
        {
                err_ret = -1;
                goto goto_err;
        }
        
        tx_cmd[0]=0x75;

        if( addr >= (1<<17) )
        {
                err_ret = -2;
                goto goto_err;

        }
        else
        {
             tx_cmd[1]=0x90|0x04|(addr>>15);    //0x94;
             tx_cmd[2]=addr>>7;    

             if(count == 512 )
            {
                    tx_cmd[3]=( addr<<1|0x0 );
                    tx_cmd[4]= 0x00;     //0x08;
            }
            else
            {
                    tx_cmd[3]=( addr<<1|count>>7 );
                    tx_cmd[4]= count;     //0x08;
            }
        }
        
        tx_cmd[5]=0x01;
        pos = 5+1;

//Add cmd respon
        memset(tx_cmd+pos,0xff,CMD_RESP_SIZE);
        pos =pos+ CMD_RESP_SIZE;

//Add token        
        tx_cmd[pos]=0xFE;
        pos = pos+1;

//Add data  
        memcpy(tx_cmd+pos,tx_data,count);
        pos = pos+count;

//Add data respon
        memset(tx_cmd+pos,0xff,DATA_RESP_SIZE_W);
        pos = pos+ DATA_RESP_SIZE_W;

        
        if( pos%8 )
        {
            len = (8 - pos%8);
            memset(tx_cmd+pos,0xff,len);
        }
        else
            len = 0;
        sif_spi_write_async_read(spi, tx_cmd,tx_cmd,pos+len);
        
        

        pos = 5+1;
        for(i=0;i<CMD_RESP_SIZE;i++)
        {
                if(tx_cmd[pos+i] == 0x00 && tx_cmd[pos+i-1] == 0xff)
                {
                        if(tx_cmd[pos+i+1] == 0x00 && tx_cmd[pos+i+2] == 0xff)
                            break;      
                }
        }
        if(i>=CMD_RESP_SIZE)
        {
                esp_dbg(ESP_DBG_ERROR, "write data resp 0x00 no recv\n");
		err_ret = -3;
                goto goto_err;
       }
        
        pos = pos+CMD_RESP_SIZE+count+1;
        for(i=0;i<DATA_RESP_SIZE_W;i++)
        {
                if(tx_cmd[pos+i] == 0xE5)
                {
			//esp_dbg(ESP_DBG_ERROR, "0xE5 pos:%d",i);
                        for(i++;i<DATA_RESP_SIZE_W;i++)
                        {
                             if( tx_cmd[pos+i] == 0xFF)
                             {
				//esp_dbg(ESP_DBG_ERROR, "0xFF pos:%d",i);
                                  break;
                             }
                        }
                        break;      
                }
        }
        if(i>=DATA_RESP_SIZE_W)
        {
                esp_dbg(ESP_DBG_ERROR, "write data resp 0xE5 no recv\n");
		err_ret = -3;
                goto goto_err;
       }

goto_err:
	return err_ret;
}

int sif_spi_write_blocks(struct spi_device *spi, unsigned int addr,unsigned char *src, int count, int check_idle)
{
	int err_ret = 0;
        int i,j;
        int n;
        int pos,len; 
        unsigned char *tx_data = (unsigned char*)src;
	u32 busy_state = 0x00;
	u32 timeout = 25000;	
	//esp_dbg(ESP_DBG_ERROR, "Block Write ---------");    
      
	ASSERT(spi != NULL);
	

#if 1  
	if (check_idle)	{
		timeout = 25000;
		do {
			busy_state = 0x00;
			sif_spi_read_raw(spi, (u8 *)&busy_state, 4);
            
                    if(busy_state != 0xffffffff)
                            esp_dbg(ESP_DBG_ERROR, "%s busy_state:%x\n", __func__, busy_state); 

		} while (busy_state != 0xffffffff && --timeout > 0);
	}

	if (timeout < 24000)
		esp_dbg(ESP_DBG_ERROR, "%s timeout[%d]\n", __func__, timeout);
#endif

//esp_dbg(ESP_DBG_ERROR, "Block Write ---------");    
        tx_cmd[0]=0x75;

        if( count <=0 )
        {
                err_ret = -1;
                goto goto_err;
        }

        if( addr >= (1<<17) )
        {
                err_ret = -2;
                goto goto_err;
        }
        else
        {
             tx_cmd[1]=0x90|0x0C|(addr>>15);   
             tx_cmd[2]=addr>>7;    

             if(count >= 512 )
            {
                    tx_cmd[3]=( addr<<1|0x0 );
                    tx_cmd[4]= 0x00;     
            }
            else
            {
                    tx_cmd[3]=( addr<<1|count>>7 );
                    tx_cmd[4]= count;     
            }
        }
        tx_cmd[5]=0x01;
        
        pos = 5+1;
//Add cmd respon
	memset(tx_cmd+pos,0xff,CMD_RESP_SIZE);
	pos =pos+ CMD_RESP_SIZE;

         for(j=0;j<count;j++)
         {
//Add token   
                tx_cmd[pos]=0xFC;
                pos = pos+1;
//Add data    
                memcpy(tx_cmd+pos,tx_data+j*SPI_BLOCK_SIZE, SPI_BLOCK_SIZE);
                pos = pos+SPI_BLOCK_SIZE;
//Add data respon
                if( j==(count- 1) )
                {
                        memset(tx_cmd+pos , 0xff , BLOCK_W_DATA_RESP_SIZE_FINAL);
                        pos = pos+ BLOCK_W_DATA_RESP_SIZE_FINAL;
                        continue;
                }
                memset(tx_cmd+pos , 0xff , BLOCK_W_DATA_RESP_SIZE_EACH);
                pos = pos+ BLOCK_W_DATA_RESP_SIZE_EACH;
         }
         
        if( pos%8 )
        {
            len = (8 - pos%8);
            memset(tx_cmd+pos,0xff,len);
        }
        else
            len = 0;
        
        sif_spi_write_async_read(spi, tx_cmd,tx_cmd,pos+len);
//Judge Write cmd resp, and 1st block data resp.        
        pos = 5+1;
        for(i=0;i<CMD_RESP_SIZE;i++)
        {
		if(tx_cmd[pos+i] == 0x00 && tx_cmd[pos+i-1] == 0xff)
                {
                        if(tx_cmd[pos+i+1] == 0x00 && tx_cmd[pos+i+2] == 0xff)
                            break;      
                }

        }
        if(i>=CMD_RESP_SIZE)
        {
                esp_dbg(ESP_DBG_ERROR, "1st block write cmd resp 0x00 no recv, %d\n", count);
                err_ret = -3;
		goto goto_err;
       }

        pos = pos+CMD_RESP_SIZE;
      
        for(j=0;j<count;j++)
        {
//Judge block data resp
                pos = pos+1;                   
                pos = pos+SPI_BLOCK_SIZE;
                
                if( j==(count-1) )
                    n = BLOCK_W_DATA_RESP_SIZE_FINAL;
                else
                    n= BLOCK_W_DATA_RESP_SIZE_EACH;
                
                for(i=0;i<n;i++)
                {
			if (tx_cmd[pos+i] != 0xff && tx_cmd[pos+i] != 0xe5)
				esp_dbg(ESP_DBG_ERROR, " %s [0x%02x] %d block ,!0xff pos:%d\n", __func__, tx_cmd[pos+i], j,i);
                        //if(tx_cmd[pos+i] == 0xE5)
                        if( (tx_cmd[pos+i]&0x0F) == 0x05 )
                        {
//esp_dbg(ESP_DBG_ERROR, "%d block ,0xE5 pos:%d",j,i);
                                for(i++;i<n;i++)
                                {
                                     if( tx_cmd[pos+i] == 0xFF)
                                     {
//esp_dbg(ESP_DBG_ERROR, "%d block ,0xFF pos:%d",j,i);
                                          break;
                                     }
                                }
                                break;     
                        }
                }
                if(i>=n)
                {	
			esp_dbg(ESP_DBG_ERROR, "%s block%d write data rsp error, %d\n", __func__, j+1, count);
                        err_ret = -4;
			goto goto_err;
               }
                pos = pos+n;   
        }
#if 0
//Add stop token     
        pos = 0;
        tx_cmd[pos]=0xFD;
        pos = pos+1;
//Add data respon
        memset(tx_cmd+pos , 0xff , BLOCK_W_DATA_RESP_SIZE_FINAL);
        pos = pos+ BLOCK_W_DATA_RESP_SIZE_FINAL;
        eagle_spi_write_async_read(tx_cmd,tx_cmd,pos);

//Judge Final busy bits.
        pos = 1;
        for(i=0;i<BLOCK_W_DATA_RESP_SIZE_FINAL;i++)
        {
                if(tx_cmd[pos+i] != 0xFF)
                {
//esp_dbg(ESP_DBG_ERROR, "Final 0x00 pos:%d",i);
                        for(i++;i<BLOCK_W_DATA_RESP_SIZE_FINAL;i++)
                        {
                             if( tx_cmd[pos+i] == 0xFF)
                             {
//esp_dbg(ESP_DBG_ERROR, "Final 0xFF pos:%d",i);
                                  break;
                             }
                        }
                        break;
                }
        }

        if(i>=BLOCK_W_DATA_RESP_SIZE_FINAL)
        {
                mutex_unlock(&RWMutex);
                esp_dbg(ESP_DBG_ERROR, "blocks final busy bit no recv");
                return -5;
       }
#endif

goto_err:

       return err_ret;
}

int sif_spi_write_mix_nosync(struct spi_device *spi, unsigned int addr, unsigned char *buf, int len, int check_idle)
{
	int blk_cnt;
	int remain_len;
	int err;

	blk_cnt = len/SPI_BLOCK_SIZE;
	remain_len = len%SPI_BLOCK_SIZE;

	if (blk_cnt > 0) {
		err  = sif_spi_write_blocks(spi, addr, buf, blk_cnt, check_idle);
		if (err) 
			return err;
	}

	if (remain_len > 0) {
		err = sif_spi_write_bytes(spi, addr, (buf + (blk_cnt*SPI_BLOCK_SIZE)), remain_len, check_idle);
		if (err)
			return err;
	}

	return 0;

}

int sif_spi_write_mix_sync(struct spi_device *spi, unsigned int addr, unsigned char *buf, int len, int check_idle) 
{
	int err;

	spi_bus_lock(spi->master);
	err = sif_spi_write_mix_nosync(spi, addr, buf, len, check_idle);
	spi_bus_unlock(spi->master);

	return err;
}

int sif_spi_epub_write_mix_nosync(struct esp_pub *epub, unsigned int addr, unsigned char *buf,int len, int check_idle)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	
	ASSERT(epub != NULL);
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

	return sif_spi_write_mix_nosync(spi, addr, buf, len, check_idle);
}

int sif_spi_epub_write_mix_sync(struct esp_pub *epub, unsigned int addr, unsigned char *buf,int len, int check_idle)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	
	ASSERT(epub != NULL);
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

	return sif_spi_write_mix_sync(spi, addr, buf, len, check_idle);
}

int sif_spi_read_bytes(struct spi_device *spi, unsigned int addr,unsigned char *dst, int count, int check_idle)
{      
	int pos,total_num,len;
        int i;
        unsigned char *rx_data = (unsigned char *)dst;
	int err_ret = 0;
      	
	u32 timeout = 25000;
 	u32 busy_state = 0x00;

	ASSERT(spi != NULL);
        

	rx_cmd[0]=0x75;
        
	if (check_idle) {	
		timeout = 25000;
		do {
			busy_state = 0x00;
			sif_spi_read_raw(spi, (u8 *)&busy_state, 4);
            
                    if(busy_state != 0xffffffff)
                            esp_dbg(ESP_DBG_ERROR, "%s busy_state:%x\n", __func__, busy_state); 
            
		} while (busy_state != 0xffffffff && --timeout > 0);
	}

	if (timeout < 24000)
		esp_dbg(ESP_DBG_ERROR, "%s timeout[%d]\n", __func__, timeout); 

        if(count > 512 )
        {
		err_ret = -1;
		goto goto_err;
        }

        if( addr >= (1<<17) )
        {
                err_ret = -2;
		goto goto_err;
        }
        else
        {
             rx_cmd[1]=0x10|0x04|(addr>>15);    //0x94;
             rx_cmd[2]=addr>>7;    

             if(count == 512 )
            {
                    rx_cmd[3]=( addr<<1|0x0 );
                    rx_cmd[4]= 0x00;     //0x08;
            }
            else
            {
                    rx_cmd[3]=( addr<<1|count>>7 );
                    rx_cmd[4]= count;     //0x08;
            }
        }
        
        rx_cmd[5]=0x01;

#if 1        // one shot read 
        total_num = CMD_RESP_SIZE+DATA_RESP_SIZE_R+count+2;
        memset(rx_cmd+6 , 0xFF ,total_num);


        if( (6+total_num)%8 )
        {
            len = (8 - (6+total_num)%8);
            memset(rx_cmd+6+total_num,0xff,len);
        }
        else
            len = 0;
        sif_spi_write_async_read(spi, rx_cmd,rx_cmd,6+total_num+len);

        pos = 5+1;
        for(i=0;i<CMD_RESP_SIZE;i++)
        {
		if(rx_cmd[pos+i] == 0x00 && rx_cmd[pos+i-1] == 0xff)
                {
                        if(rx_cmd[pos+i+1] == 0x00 && rx_cmd[pos+i+2] == 0xff)
                            break;      
                }
        }
        if(i>=CMD_RESP_SIZE)
        {
                esp_dbg(ESP_DBG_ERROR, "read cmd resp 0x00 no recv\n");
/***********error info************************/
/*
	char t = pos;
	while ( t < pos+32) {
		printk(KERN_ERR "rx:[0x%02x] ", rx_cmd[t]);
		t++;
		if ((t-pos)%8 == 0)
			printk(KERN_ERR "\n");
	}
*/
                err_ret = -3;
		goto goto_err;
       }
//esp_dbg(ESP_DBG_ERROR, "0x00 pos:%d",pos+i);
       pos = pos+i+2;
       
        for(i=0;i<DATA_RESP_SIZE_R;i++)
        {
                if(rx_cmd[pos+i]==0xFE)
                    break;
        }
        if(i>=DATA_RESP_SIZE_R)
        {
                esp_dbg(ESP_DBG_ERROR, "read data resp 0xFE no recv\n");
		err_ret = -4;
		goto goto_err;
       }
//esp_dbg(ESP_DBG_ERROR, "0xFE pos:%d",pos+i);
       pos = pos+i+1;
        memcpy(rx_data,rx_cmd+pos,count);
#else       //wait method
  
        memset(rx_cmd+6,0xFF,CMD_RESP_SIZE+DATA_RESP_SIZE_R+count+2);
//printk(KERN_ERR "mark 1",pos+i);     
        sif_spi_write_async_read(spi, rx_cmd,rx_cmd,6+CMD_RESP_SIZE+DATA_RESP_SIZE_R+count+2);
//printk(KERN_ERR "mark 2",pos+i);
        pos = 5+1;
        for(i=0;i<CMD_RESP_SIZE;i++)
        {
		if(rx_cmd[pos+i] == 0x00 && rx_cmd[pos+i-1] == 0xff)
                {
                        if(rx_cmd[pos+i+1] == 0x00 && rx_cmd[pos+i+2] == 0xff)
                            break;      
                }
        }
        if(i>=CMD_RESP_SIZE)
        {
                printk(KERN_ERR "read cmd resp 0x00 no recv\n");
                //kfree(rx_cmd);
                err_ret = -3;
		goto goto_err;
       }
//printk(KERN_ERR "0x00 pos:%d",pos+i);
       pos = pos+i+2;
       
        for(i=0;i<DATA_RESP_SIZE_R;i++)
        {
                if(rx_cmd[pos+i]==0xFE)
                    break;
        }
        if(i>=DATA_RESP_SIZE_R)
        {
                printk(KERN_ERR "read data resp 0xFE no recv\n");
		err_ret = -4;
		goto goto_err;
       }
//printk(KERN_ERR "0xFE pos:%d",pos+i);
       pos = pos+i+1;

        for(i=0;i<count;i++)
        {
                *(rx_data+i) = rx_cmd[pos+i];
        }


#endif
goto_err:

        return err_ret;
}

int sif_spi_read_blocks(struct spi_device *spi, unsigned int addr, unsigned char *dst, int count, int check_idle)
{
	int err_ret = 0;
	int pos,len;
	int i,j;
	unsigned char *rx_data = (unsigned char *)dst;
       int total_num;
	u32 busy_state = 0x00;
	u32 timeout = 25000;

	//esp_dbg(ESP_DBG_ERROR, "Block Read ---------");               
	ASSERT(spi != NULL);
   

#if 1 
	if (check_idle)	{
		timeout = 25000;
		do {
			busy_state = 0x00;
			sif_spi_read_raw(spi, (u8 *)&busy_state, 4);
            
                    if(busy_state != 0xffffffff)
                            esp_dbg(ESP_DBG_ERROR, "%s busy_state:%x\n", __func__, busy_state); 
 
		} while (busy_state != 0xffffffff && --timeout > 0);
	}

	if (timeout < 24000)
		esp_dbg(ESP_DBG_ERROR, "%s timeout[%d]\n", __func__, timeout);
#endif
        rx_cmd[0]=0x75;

        if( count <=0 )
        {
                err_ret = -1;
                goto goto_err;
        }
        if( addr >= (1<<17) )
        {
                err_ret = -2;
		goto goto_err;
        }
        else
        {
             rx_cmd[1]=0x10|0x0C|(addr>>15);   
             rx_cmd[2]=addr>>7;    

            if(count >= 512 )
            {
                    rx_cmd[3]=( addr<<1|0x0 );
                    rx_cmd[4]= 0x00;     
            }
            else
            {
                    rx_cmd[3]=( addr<<1|count>>7 );
                    rx_cmd[4]= count;     
            }
        }
        rx_cmd[5]=0x01;
       total_num = CMD_RESP_SIZE+BLOCK_R_DATA_RESP_SIZE_1ST+SPI_BLOCK_SIZE+ 2 + (count-1)*(BLOCK_R_DATA_RESP_SIZE_EACH+SPI_BLOCK_SIZE+2);
	memset(rx_cmd+6, 0xFF ,total_num);
    
        if( (6+total_num)%8 )
        {
            len = (8 - (6+total_num)%8);
            memset(rx_cmd+6+total_num,0xff,len);
        }
        else
            len = 0;
        
        sif_spi_write_async_read(spi, rx_cmd,rx_cmd,6+total_num+len );
        
        pos = 5+1;
        for(i=0;i<CMD_RESP_SIZE;i++)
        {
		if(rx_cmd[pos+i] == 0x00 && rx_cmd[pos+i-1] == 0xff)
                {
                        if(rx_cmd[pos+i+1] == 0x00 && rx_cmd[pos+i+2] == 0xff)
                            break;      
                }

        }
        if(i>=CMD_RESP_SIZE)
        {
		esp_dbg(ESP_DBG_ERROR, "block read cmd resp 0x00 no recv\n");
#if 0
	char t = pos;
	while ( t < pos+32) {
		printk(KERN_ERR "rx:[0x%02x] ", rx_cmd[t]);
		t++;
		if ((t-pos)%8 == 0)
			printk(KERN_ERR "\n");
	}
#endif

                err_ret = -3;
		goto goto_err;
        }

        pos = pos+i+2;

	for(i=0;i<BLOCK_R_DATA_RESP_SIZE_1ST;i++)
        {
                if(rx_cmd[pos+i]==0xFE)
                {
//esp_dbg(ESP_DBG_ERROR, "0xFE pos:%d",i);
                    break;
                }
        }
        if(i>=BLOCK_R_DATA_RESP_SIZE_1ST)
        {
                esp_dbg(ESP_DBG_ERROR, "1st block read data resp 0xFE no recv\n");
                err_ret = -4;
		goto goto_err;
        }

        pos = pos+i+1;

        memcpy(rx_data,rx_cmd+pos,SPI_BLOCK_SIZE);

        pos = pos +SPI_BLOCK_SIZE + 2;

        for(j=1;j<count;j++)
        {

        	for(i=0;i<BLOCK_R_DATA_RESP_SIZE_EACH;i++)
        	{
               		if(rx_cmd[pos+i]==0xFE)
                	{
			//esp_dbg(ESP_DBG_ERROR, "0xFE pos:%d",i);
                    	break;
                	}
        	}
        	if(i>=BLOCK_R_DATA_RESP_SIZE_EACH)
        	{
                	esp_dbg(ESP_DBG_ERROR, "block%d read data resp 0xFE no recv,total:%d\n",j+1,count);
                        err_ret = -4;
			goto goto_err;
                }

                pos = pos+i+1;
 
                memcpy(rx_data+j*SPI_BLOCK_SIZE,rx_cmd+pos,SPI_BLOCK_SIZE);
                
		pos = pos +SPI_BLOCK_SIZE + 2;
        }
     
goto_err:       

	return err_ret;
}

int sif_spi_read_mix_nosync(struct spi_device *spi, unsigned int addr, unsigned char *buf, int len, int check_idle)
{
	int blk_cnt;
	int remain_len;
	int err;

	blk_cnt = len/SPI_BLOCK_SIZE;
	remain_len = len%SPI_BLOCK_SIZE;

	if (blk_cnt > 0) {
		err  = sif_spi_read_blocks(spi, addr, buf, blk_cnt, check_idle);
		if (err) 
			return err;
	}

	if (remain_len > 0) {
		err = sif_spi_read_bytes(spi, addr, (buf + (blk_cnt*SPI_BLOCK_SIZE)), remain_len, check_idle);
		if (err)
			return err;
	}

	return 0;
}

int sif_spi_read_mix_sync(struct spi_device *spi, unsigned int addr, unsigned char *buf, int len, int check_idle)
{
	int err;
	
	spi_bus_lock(spi->master);
	err = sif_spi_read_mix_nosync(spi, addr, buf, len, check_idle);
	spi_bus_unlock(spi->master);

	return err;
}

int sif_spi_epub_read_mix_nosync(struct esp_pub *epub, unsigned int addr, unsigned char *buf,int len, int check_idle)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	
	ASSERT(epub != NULL);
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

	return sif_spi_read_mix_nosync(spi, addr, buf, len, check_idle);
}

int sif_spi_epub_read_mix_sync(struct esp_pub *epub, unsigned int addr, unsigned char *buf,int len, int check_idle)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	
	ASSERT(epub != NULL);
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

	return sif_spi_read_mix_sync(spi, addr, buf, len, check_idle);
}

int sif_spi_read_sync(struct esp_pub *epub, unsigned char *buf, int len, int check_idle)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	u32 read_len;

        ASSERT(epub != NULL);
        ASSERT(buf != NULL);

        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

        switch(sctrl->target_id) {
        case 0x100:
                read_len = len;
                break;
        case 0x600:
                read_len = roundup(len, sctrl->slc_blk_sz);
                break;
        default:
                read_len = len;
                break;
        }
	
	return sif_spi_read_mix_sync(spi, sctrl->slc_window_end_addr - 2 - (len), buf, read_len, check_idle);
}

int sif_spi_write_sync(struct esp_pub *epub, unsigned char *buf, int len, int check_idle)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
        u32 write_len;

        ASSERT(epub != NULL);
        ASSERT(buf != NULL);

        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

        switch(sctrl->target_id) {
        case 0x100:
                write_len = len;
                break;
        case 0x600:
                write_len = roundup(len, sctrl->slc_blk_sz);
                break;
        default:
                write_len = len;
                break;
        }
	return sif_spi_write_mix_sync(spi, sctrl->slc_window_end_addr - (len), buf, write_len, check_idle);
}

int sif_spi_read_nosync(struct esp_pub *epub, unsigned char *buf, int len, int check_idle, bool noround)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	u32 read_len;

        ASSERT(epub != NULL);
        ASSERT(buf != NULL);

        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

        switch(sctrl->target_id) {
        case 0x100:
                read_len = len;
                break;
        case 0x600:
		if (!noround)
                	read_len = roundup(len, sctrl->slc_blk_sz);
		else
			read_len = len;
                break;
        default:
                read_len = len;
                break;
        }
	
	return sif_spi_read_mix_nosync(spi, sctrl->slc_window_end_addr - 2 - (len), buf, read_len, check_idle);
}

int sif_spi_write_nosync(struct esp_pub *epub, unsigned char *buf, int len, int check_idle)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
        u32 write_len;

        ASSERT(epub != NULL);
        ASSERT(buf != NULL);

        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

        switch(sctrl->target_id) {
        case 0x100:
                write_len = len;
                break;
        case 0x600:
                write_len = roundup(len, sctrl->slc_blk_sz);
                break;
        default:
                write_len = len;
                break;
        }
	return sif_spi_write_mix_nosync(spi, sctrl->slc_window_end_addr - (len), buf, write_len, check_idle);
}

void sif_spi_protocol_init(struct spi_device *spi)
{
	unsigned char spi_proto_ini_status = 0;
        unsigned char rx_buf1[10];
        unsigned char tx_buf1[10];
        unsigned char dummy_tx_buf[10];
        memset(dummy_tx_buf,0xff,sizeof(dummy_tx_buf));      
        do
        {            
                 if( spi_proto_ini_status == 0 )
                 {
                        do
                        { 
                                tx_buf1[0]=0x40;
                                tx_buf1[1]=0x00;
                                tx_buf1[2]=0x00;
                                tx_buf1[3]=0x00;
                                tx_buf1[4]=0x00;
                                tx_buf1[5]=0x95;
                                //printf("CMD0 \n");
                                sif_spi_write_raw(spi, tx_buf1, 6);
                                sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                            esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                                mdelay(100);
                        }while( rx_buf1[2] != 0x01 );
                        //  }while(1);
                 }
                else if( spi_proto_ini_status == 1 )
                {
                        tx_buf1[0]=0x45;
                        tx_buf1[1]=0x00;
                        tx_buf1[2]=0x20;               //0x04;
                        tx_buf1[3]=0x00;
                        tx_buf1[4]=0x00;
                        tx_buf1[5]=0x01;
                        //spi_err("CMD 5 1st\n");
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                 }
                else if( spi_proto_ini_status == 2 )
                {
                        tx_buf1[0]=0x45;
                        tx_buf1[1]=0x00;
                        tx_buf1[2]=0x20;               
                        tx_buf1[3]=0x00;
                        tx_buf1[4]=0x00;
                        tx_buf1[5]=0x01;
                        //spi_err("CMD5 2nd\n");
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
               }
               else if( spi_proto_ini_status == 3 )                  //CMD 52   addr 0x2,   data 0x02;
                {
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x80;
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x04;
                        tx_buf1[4]=0x02;
                        tx_buf1[5]=0x01;
                        //spi_err("CMD52 Write  addr 02 \n");  
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                 }
                else if( spi_proto_ini_status == 4 )           
                {
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x80;
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x08;
                        tx_buf1[4]=0x03;
                        tx_buf1[5]=0x01;
                        //spi_err("CMD52 Write  addr 04 \n"); 
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                 } 
                else if( spi_proto_ini_status == 5 )           
                {
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x00;
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x04;
                        tx_buf1[4]=0x00;
                        tx_buf1[5]=0x01;
                        //spi_err("CMD52 Read  addr 0x2 \n");
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                                      
                 }
                 else if( spi_proto_ini_status == 6 )           
                {
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x00;
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x08;
                        tx_buf1[4]=0x00;
                        tx_buf1[5]=0x01;
                        //spi_err("CMD52 Read addr 0x4 \n");
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                 }
                 else if (spi_proto_ini_status>6 && spi_proto_ini_status<15)
                {
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x10;
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0xF0+2*(spi_proto_ini_status-7);
                        tx_buf1[4]=0x00;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                 }
                else if (spi_proto_ini_status==15)
                {
                        //spi_err("CMD52 Write  Reg addr 0x110 \n"); 
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x80;              //func0 should be
                        tx_buf1[2]=0x02;               
                        tx_buf1[3]=0x20;
                        tx_buf1[4]=(unsigned char)(SPI_BLOCK_SIZE & 0xff);                       //0x02;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                        
                        //spi_err("CMD52 Write Reg addr 0x111 \n");  
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x80;
                        tx_buf1[2]=0x02;               
                        tx_buf1[3]=0x22;
                        tx_buf1[4]=(unsigned char)(SPI_BLOCK_SIZE>>8);                      //0x00;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);

                        //spi_err("CMD52 Write Reg addr 0x111 \n");   /* set boot mode */
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x80;
                        tx_buf1[2]=0x41;               
                        tx_buf1[3]=0xe0;
                        tx_buf1[4]=0x01;          //0x00;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                       esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);

                }
                else if (spi_proto_ini_status==16)
                {
#if 0                                                        
                        //printf("CMD52 Write  Reg addr 0x40 \n"); 
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x90;              
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x80;
                        tx_buf1[4]=0x91;                       //0x02;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                
                        //sif_spi_read_bytes( 0x0c,rx_buf1, 4);

                        //printf("CMD52 Write  Reg addr 0x3c \n"); 
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x90;            
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x78;
                        tx_buf1[4]=0x3f;                     
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                        esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                        
                                                                //printf("CMD52 Write  Reg addr 0x3d \n"); 
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x90;              
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x7a;
                        tx_buf1[4]=0x34;                        //0x02;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                        esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                        
                                                               // printf("CMD52 Write  Reg addr 0x3e \n"); 
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x90;              
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x7c;
                        tx_buf1[4]=0xfe;                       //0x02;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                        esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                        
                                                                //printf("CMD52 Write  Reg addr 0x3f \n"); 
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x90;              
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x7e;
                        tx_buf1[4]=0x00;                       //0x02;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                        esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                        
                                                                //printf("CMD52 Write  Reg addr 0x40 \n"); 
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x90;              
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x80;
                        tx_buf1[4]=0xd1;                      //0x02;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                        esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);

                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x90;              
                        tx_buf1[2]=0x00;               
                        tx_buf1[3]=0x52;
                        tx_buf1[4]=0x30;                      //0x02;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                        esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
                ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                        
#endif                    
                }
                else
                {
                        break;
               }
                                            //  mdelay(500);
                   spi_proto_ini_status++;   
        } while (1);
}

int sif_spi_read_reg(struct spi_device *spi, unsigned int addr, unsigned char *value )
{
        unsigned char tx_cmd[6];
        unsigned char rx_cmd[20];
        int err_ret = 0;


        tx_cmd[0]=0x74;

        if( addr >= (1<<17) )
        {
                err_ret = -1;
		goto goto_err;
                //return err_ret;
        }
        else
        {
                tx_cmd[1]=0x10|0x00|(addr>>15);    
                tx_cmd[2]=addr>>7;    

                tx_cmd[3]=( addr<<1 );
                tx_cmd[4]= 0x00;    
        }
        
        tx_cmd[5]=0x01;

        sif_spi_write_raw(spi, tx_cmd,6);
                //printf("CMD52 Read \n");
#if 0
//Response read
      
        sif_spi_read_raw(spi, rx_cmd,20);  
        
                                       esp_dbg(ESP_DBG_ERROR, "write resp:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_cmd[0],rx_cmd[1]
                ,rx_cmd[2],rx_cmd[3],rx_cmd[4],rx_cmd[5],rx_cmd[6],rx_cmd[7],rx_cmd[8],rx_cmd[9]);
        return 0;
 #else

 //Read Response.        
        do
        {
                rx_cmd[0]=0xFF;
                sif_spi_read_raw(spi, rx_cmd,1);
         }while(rx_cmd[0]==0xFF);
        rx_cmd[1]=0xFF;
                sif_spi_read_raw(spi, rx_cmd+1,1);
        
          esp_dbg(ESP_DBG_ERROR, "read resp:[0x%02x],[0x%02x]\n",rx_cmd[0],rx_cmd[1]);

        if(rx_cmd[0]!=0)
            err_ret = -1;
        else
            err_ret = 0;

        *value = rx_cmd[1]; 
	
        //return err_ret;       
 #endif

goto_err:

        return err_ret;       
}


int sif_spi_write_reg(struct spi_device *spi, unsigned int addr, unsigned char value)
{
        unsigned char tx_cmd[6];
        unsigned char rx_cmd[20];
        int err_ret = 0;
       

        
	tx_cmd[0]=0x74;

        if( addr >= (1<<17) )
        {
                err_ret = -1;
		goto goto_err;
                //return err_ret;
        }
        else
        {
                tx_cmd[1]=0x90|0x00|(addr>>15);    //0x94;
                tx_cmd[2]=addr>>7;    

                tx_cmd[3]=( addr<<1 );
                tx_cmd[4]= value;
        }
        
        tx_cmd[5]=0x01;
        sif_spi_write_raw(spi, tx_cmd,6);
                //printf("CMD52 <Write config> stauts=16 \n");


#if 0
        //memset(rx_cmd,0xFF,20);
                sif_spi_read_raw(spi, rx_cmd,20);

                                       esp_dbg(ESP_DBG_ERROR, "write resp:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_cmd[0],rx_cmd[1]
                ,rx_cmd[2],rx_cmd[3],rx_cmd[4],rx_cmd[5],rx_cmd[6],rx_cmd[7],rx_cmd[8],rx_cmd[9]);
        return 0;
#else

        do
        {
                rx_cmd[0]=0xFF;
                sif_spi_read_raw(spi, rx_cmd,1);
         }while(rx_cmd[0]==0xFF);
        rx_cmd[1]=0xFF;
                sif_spi_read_raw(spi, rx_cmd+1,1);

         if( rx_cmd[1]!=value )    
         {
                //printf("Write error,%X,%X", rx_cmd[0],rx_cmd[1]);
                 esp_dbg(ESP_DBG_ERROR, "write resp:[0x%02x],[0x%02x]\n",rx_cmd[0],rx_cmd[1]);
                err_ret = -1;
		goto goto_err;
               
         }

goto_err:

        return err_ret;
        
#endif
//Read Response.        

}

static int spi_irq_thread(void *data)
{
	struct spi_device *spi = (struct spi_device *)data;

	do {
		sif_dsr(spi);
		
		set_current_state(TASK_INTERRUPTIBLE);
		sif_platform_irq_mask(0);

		if (!kthread_should_stop())
			schedule();

		set_current_state(TASK_RUNNING);

	} while (!kthread_should_stop());
	return 0;	
}

int sif_setup_irq_thread(struct spi_device *spi)
{
	sif_irq_thread = kthread_run(spi_irq_thread, spi, "kspiirqd/eagle");
	if (IS_ERR(sif_irq_thread)) {
		esp_dbg(ESP_DBG_ERROR, "setup irq thread error!\n");
		return -1;
	}
	return 0;
}

static irqreturn_t sif_irq_handler(int irq, void *dev_id)
{
	sif_platform_irq_mask(1);

	if (sif_platform_is_irq_occur()) {
		wake_up_process(sif_irq_thread);
	} else {
		sif_platform_irq_mask(0);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

void sif_enable_irq(struct esp_pub *epub) 
{
        int err;
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;

        ASSERT(epub != NULL);

        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);

	mdelay(100);

	sif_platform_irq_init();
	err = sif_setup_irq_thread(spi);
        if (err) {
                esp_dbg(ESP_DBG_ERROR, "%s setup sif irq failed\n", __func__);
		return;
	}

	err = request_irq(sif_platform_get_irq_no(), sif_irq_handler, IRQF_TRIGGER_LOW, "esp_spi_irq", spi);
	//err = request_irq(sif_platform_get_irq_no(), sif_irq_handler, IRQF_TRIGGER_FALLING, "esp_spi_irq", spi);

        if (err) {
                esp_dbg(ESP_DBG_ERROR, "sif %s failed\n", __func__);
		return ;
	}

        atomic_set(&sctrl->irq_installed, 1);
}

void sif_disable_irq(struct esp_pub *epub) 
{
	int i = 0;
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;

        ASSERT(epub != NULL);

        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
        ASSERT(spi != NULL);


        if (atomic_read(&sctrl->irq_installed) == 0)
                return;
        
        while (atomic_read(&sctrl->irq_handling)) {
                schedule_timeout(HZ / 100);
                if (i++ >= 400) {
                        esp_dbg(ESP_DBG_ERROR, "%s force to stop irq\n", __func__);
                        break;
                }
        }

        free_irq(sif_platform_get_irq_no(), spi);
	kthread_stop(sif_irq_thread);

	sif_platform_irq_deinit();

        atomic_set(&sctrl->irq_installed, 0);
}


int esp_setup_spi(struct spi_device *spi)
{
	/**** alloc buffer for spi io */
	if (sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT) {
		buf_addr = (unsigned char *)kmalloc (MAX_BUF_SIZE, GFP_KERNEL);
		if (buf_addr == NULL)
			return -ENOMEM;

		tx_cmd = buf_addr;
       		rx_cmd = buf_addr;
	}

#if 0
       spi->mode = 0x03;
	spi->bits_per_word = 8;
	spi->max_speed_hz = SPI_FREQ;

	return spi_setup(spi);
#endif
	return 0;
}


static int esp_spi_probe(struct spi_device *spi);
static int esp_spi_remove(struct spi_device *spi); 

static int esp_spi_probe(struct spi_device *spi) 
{
        int err;
        struct esp_pub *epub;
        struct esp_spi_ctrl *sctrl;

        esp_dbg(ESP_DBG_ERROR, "%s enter\n", __func__);
	
	err = esp_setup_spi(spi);
	if (err) {
		esp_dbg(ESP_DBG_ERROR, "%s setup_spi error[%d]\n", __func__, err);
		goto _err_spi;
	}
	esp_dbg(ESP_DBG_ERROR, "%s init_protocol\n", __func__);
	sif_spi_protocol_init(spi);

	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		sctrl = kzalloc(sizeof(struct esp_spi_ctrl), GFP_KERNEL);

		if (sctrl == NULL) {
			assert(0);
			return -ENOMEM;
		}

		/* temp buffer reserved for un-dma-able request */
		sctrl->dma_buffer = kzalloc(ESP_DMA_IBUFSZ, GFP_KERNEL);

		if (sctrl->dma_buffer == NULL) {
			assert(0);
			goto _err_last;
		}
		sif_sctrl = sctrl;
        	sctrl->slc_blk_sz = SIF_SLC_BLOCK_SIZE;
        	
		epub = esp_pub_alloc_mac80211(&spi->dev);

        	if (epub == NULL) {
                	esp_dbg(ESP_DBG_ERROR, "no mem for epub \n");
                	err = -ENOMEM;
                	goto _err_dma;
        	}
        	epub->sif = (void *)sctrl;
        	sctrl->epub = epub;
	
#ifdef USE_EXT_GPIO	
		err = ext_gpio_init(epub);
		if (err) {
                	esp_dbg(ESP_DBG_ERROR, "ext_irq_work_init failed %d\n", err);
			return err;
		}
#endif
			
	} else {
		ASSERT(sif_sctrl != NULL);
		sctrl = sif_sctrl;
		sif_sctrl = NULL;
		epub = sctrl->epub;
		SET_IEEE80211_DEV(epub->hw, &spi->dev);
		epub->dev = &spi->dev;
	}

        epub->sdio_state = sif_sdio_state;

        sctrl->spi = spi;
        spi_set_drvdata(spi, sctrl);

        if (err){
                if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT)
                	goto _err_epub;
		  else
			goto _err_second_init;
        }
        check_target_id(epub);

#ifdef SDIO_TEST
        sif_test_tx(sctrl);
#else
        err = esp_pub_init_all(epub);

        if (err) {
                esp_dbg(ESP_DBG_ERROR, "esp_init_all failed: %d\n", err);
                if(sif_sdio_state == ESP_SDIO_STATE_SECOND_INIT)
			goto _err_second_init;
        }

#endif //SDIO_TEST
        esp_dbg(ESP_DBG_TRACE, " %s return  %d\n", __func__, err);
	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		esp_dbg(ESP_DBG_ERROR, "first normal exit\n");
		sif_sdio_state = ESP_SDIO_STATE_FIRST_NORMAL_EXIT;
		up(&esp_powerup_sem);
	}

        return err;

_err_epub:
        esp_pub_dealloc_mac80211(epub);
_err_dma:
        kfree(sctrl->dma_buffer);
_err_last:
        kfree(sctrl);
	if (buf_addr) {
		kfree(buf_addr);
		buf_addr = NULL;
		tx_cmd = NULL;
		rx_cmd = NULL;
	}
	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		sif_sdio_state = ESP_SDIO_STATE_FIRST_ERROR_EXIT;
		up(&esp_powerup_sem);
	}
        return err;
_err_second_init:
	sif_sdio_state = ESP_SDIO_STATE_SECOND_ERROR_EXIT;
	esp_spi_remove(spi);
	return err;
_err_spi:
	if (buf_addr) {
		kfree(buf_addr);
		buf_addr = NULL;
		tx_cmd = NULL;
		rx_cmd = NULL;
	}
	return err;
}

static int esp_spi_remove(struct spi_device *spi) 
{
        struct esp_spi_ctrl *sctrl = NULL;

        sctrl = spi_get_drvdata(spi);

        if (sctrl == NULL) {
                esp_dbg(ESP_DBG_ERROR, "%s no sctrl\n", __func__);
                return -1;
        }

        do {
                if (sctrl->epub == NULL) {
                        esp_dbg(ESP_DBG_ERROR, "%s epub null\n", __func__);
                        break;
                }
		sctrl->epub->sdio_state = sif_sdio_state;
		if(sif_sdio_state != ESP_SDIO_STATE_FIRST_NORMAL_EXIT){
			do{
				int err;
				sif_lock_bus(sctrl->epub);
				err = sif_interrupt_target(sctrl->epub, 7);
				sif_unlock_bus(sctrl->epub);
			}while(0);
	
                	if (sctrl->epub->sip) {
                        	sip_detach(sctrl->epub->sip);
                        	sctrl->epub->sip = NULL;
                        	esp_dbg(ESP_DBG_TRACE, "%s sip detached \n", __func__);
#ifdef USE_EXT_GPIO	
				ext_gpio_deinit();
#endif
                	}
		} else {
			//sif_disable_target_interrupt(sctrl->epub);
			atomic_set(&sctrl->epub->sip->state, SIP_STOP);
			sif_disable_irq(sctrl->epub);
		}

#ifdef TEST_MODE
                test_exit_netlink();
#endif /* TEST_MODE */
		if(sif_sdio_state != ESP_SDIO_STATE_FIRST_NORMAL_EXIT){
                	esp_pub_dealloc_mac80211(sctrl->epub);
                	esp_dbg(ESP_DBG_TRACE, "%s dealloc mac80211 \n", __func__);
			
			if (sctrl->dma_buffer) {
				kfree(sctrl->dma_buffer);
				sctrl->dma_buffer = NULL;
				esp_dbg(ESP_DBG_TRACE, "%s free dma_buffer \n", __func__);
			}

			kfree(sctrl);

			if (buf_addr) {
				kfree(buf_addr);
				buf_addr = NULL;
				rx_cmd = NULL;
				tx_cmd = NULL;
			}

		}

        } while (0);
        
	spi_set_drvdata(spi,NULL);
	
        esp_dbg(ESP_DBG_TRACE, "eagle spi remove complete\n");

	return 0;
}

static int esp_spi_suspend(struct device *dev)
{        return 0;

}

static int esp_spi_resume(struct device *dev)
{
        esp_dbg(ESP_DBG_ERROR, "%s", __func__);

        return 0;
}

static const struct dev_pm_ops esp_spi_pm_ops = {
        .suspend= esp_spi_suspend,
        .resume= esp_spi_resume,
};

extern struct spi_device_id esp_spi_id[];

struct spi_driver esp_spi_driver = {
	.id_table = esp_spi_id,
	.driver	= {
		.name	= "eagle",
		.bus    = &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= esp_spi_probe,
	.remove	= esp_spi_remove,
};

static int esp_spi_dummy_probe(struct spi_device *spi)
{
        esp_dbg(ESP_DBG_ERROR, "%s enter\n", __func__);

        up(&esp_powerup_sem);
        
        return 0;
}

static int esp_spi_dummy_remove(struct spi_device *spi) 
{
        return 0;
}

struct spi_driver esp_spi_dummy_driver = {
	.id_table = esp_spi_id,
	.driver	= {
		.name	= "eagle_dummy",
		.bus    = &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= esp_spi_dummy_probe,
	.remove	= esp_spi_dummy_remove,
};

static int __init esp_spi_init(void) 
{
#define ESP_WAIT_UP_TIME_MS 11000
        int err;
        u64 ver;
        int retry = 3;
        bool powerup = false;
        int edf_ret = 0;

        esp_dbg(ESP_DBG_TRACE, "%s \n", __func__);

#ifdef REGISTER_SPI_BOARD_INFO
	sif_platform_register_board_info();
#endif

#ifdef DRIVER_VER
        ver = DRIVER_VER;
        esp_dbg(ESP_SHOW, "\n*****%s %s EAGLE DRIVER VER:%llx*****\n\n", __DATE__, __TIME__, ver);
#endif
        edf_ret = esp_debugfs_init();

#ifdef ANDROID
	android_request_init_conf();
#endif /* defined(ANDROID)*/

        esp_wakelock_init();
        esp_wake_lock();

        do {
                sema_init(&esp_powerup_sem, 0);

                sif_platform_target_poweron();

                err = spi_register_driver(&esp_spi_dummy_driver);
                if (err) {
                        esp_dbg(ESP_DBG_ERROR, "eagle spi driver registration failed, error code: %d\n", err);
                        goto _fail;
                }

                if (down_timeout(&esp_powerup_sem,
                                 msecs_to_jiffies(ESP_WAIT_UP_TIME_MS)) == 0) 
		{

                        powerup = true;
			msleep(200);
                        break;
                }

                esp_dbg(ESP_SHOW, "%s ------ RETRY ------ \n", __func__);

		sif_record_retry_config();

                spi_unregister_driver(&esp_spi_dummy_driver);

                sif_platform_target_poweroff();
                
        } while (retry--);

        if (!powerup) {
                esp_dbg(ESP_DBG_ERROR, "eagle spi can not power up!\n");

                err = -ENODEV;
                goto _fail;
        }

        esp_dbg(ESP_SHOW, "%s power up OK\n", __func__);

        spi_unregister_driver(&esp_spi_dummy_driver);

        sif_sdio_state = ESP_SDIO_STATE_FIRST_INIT;
        sema_init(&esp_powerup_sem, 0);

        spi_register_driver(&esp_spi_driver);

        if (down_timeout(&esp_powerup_sem,
                                 msecs_to_jiffies(ESP_WAIT_UP_TIME_MS)) == 0) 
	{
		if(sif_sdio_state == ESP_SDIO_STATE_FIRST_NORMAL_EXIT){
                	spi_unregister_driver(&esp_spi_driver);

			msleep(80);
                
			sif_sdio_state = ESP_SDIO_STATE_SECOND_INIT;
        	
			spi_register_driver(&esp_spi_driver);
		}
                
        }


        esp_register_early_suspend();
	esp_wake_unlock();
        return err;

_fail:
        esp_wake_unlock();
        esp_wakelock_destroy();

        return err;
}

static void __exit esp_spi_exit(void) 
{
	esp_dbg(ESP_DBG_TRACE, "%s \n", __func__);

	esp_debugfs_exit();
	
        esp_unregister_early_suspend();

	spi_unregister_driver(&esp_spi_driver);
	
#ifndef FPGA_DEBUG
	sif_platform_target_poweroff();
#endif /* !FPGA_DEBUG */

        esp_wakelock_destroy();
}

MODULE_AUTHOR("Espressif System");
MODULE_DESCRIPTION("Driver for SPI interconnected eagle low-power WLAN devices");
MODULE_LICENSE("GPL");
#endif /* ESP_USE_SPI */
