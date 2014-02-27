#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>
#include <linux/mmc/sdio_func.h>

#include "rda5890_defs.h"
#include "rda5890_dev.h"
#include "rda5890_wid.h"
#include "rda5890_wext.h"
#include "rda5890_txrx.h"
#include "rda5890_if_sdio.h"

static unsigned char is_need_set_notch = 0;

/* for both Query and Write */
int rda5890_wid_request(struct rda5890_private *priv, 
		char *wid_req, unsigned short wid_req_len,
		char *wid_rsp, unsigned short *wid_rsp_len)
{
	int ret = 0;
	int timeleft = 0;
	char data_buf[RDA5890_MAX_WID_LEN + 2];
	unsigned short data_len;
    struct if_sdio_card * card = (struct if_sdio_card *)priv->card;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
		
#ifdef WIFI_TEST_MODE
  if(rda_5990_wifi_in_test_mode())
      return 0;
#endif  //end WIFI_TEST_MODE

	mutex_lock(&priv->wid_lock);
	priv->wid_rsp = wid_rsp;
	priv->wid_rsp_len = *wid_rsp_len;
	priv->wid_pending = 1;

	data_len = wid_req_len + 2;
	data_buf[0] = (char)(data_len&0xFF);
	data_buf[1] = (char)((data_len>>8)&0x0F);
	data_buf[1] |= 0x40;  // for Request(Q/W) 0x4
	memcpy(data_buf + 2, wid_req, wid_req_len);

	init_completion(&priv->wid_done);
#ifdef WIFI_UNLOCK_SYSTEM
    rda5990_wakeLock();
#endif
	ret = rda5890_host_to_card(priv, data_buf, data_len, WID_REQUEST_PACKET);
	if (ret) {
		RDA5890_ERRP("host_to_card send failed, ret = %d\n", ret);
		priv->wid_pending = 0;
		goto out;
	}

    atomic_inc(&card->wid_complete_flag);
	timeleft = wait_for_completion_timeout(&priv->wid_done, msecs_to_jiffies(450)); 
	if (timeleft == 0) {
		RDA5890_ERRP("respose timeout wid :%x %x \n", wid_req[4], wid_req[5]);
		priv->wid_pending = 0;
		ret = -EFAULT; 
		goto out;
	}

	*wid_rsp_len = priv->wid_rsp_len;

out:
	mutex_unlock(&priv->wid_lock);
    atomic_set(&card->wid_complete_flag, 0);
	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s >>> wid: %x %x \n", __func__, wid_req[4],wid_req[5]);
#ifdef WIFI_UNLOCK_SYSTEM
    rda5990_wakeUnlock();
#endif
	return ret;
}

int rda5890_wid_request_polling(struct rda5890_private *priv, 
		char *wid_req, unsigned short wid_req_len,
		char *wid_rsp, unsigned short *wid_rsp_len)
{
	int ret = -1;
	int timeleft = 0;
	char data_buf[RDA5890_MAX_WID_LEN + 2];
	unsigned short data_len;
	unsigned char status;
	unsigned int retry = 0, count = 0;
    struct if_sdio_card * card = (struct if_sdio_card*)priv->card;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	priv->wid_rsp = wid_rsp;
	priv->wid_rsp_len = *wid_rsp_len;

	data_len = wid_req_len + 2;
	data_buf[0] = (char)(data_len&0xFF);
	data_buf[1] = (char)((data_len>>8)&0x0F);
	data_buf[1] |= 0x40;  // for Request(Q/W) 0x4
	memcpy(data_buf + 2, wid_req, wid_req_len);

re_send:

    count += 1;
	ret = rda5890_host_to_card(priv, data_buf, data_len, WID_REQUEST_POLLING_PACKET);
	if (ret) {
		RDA5890_ERRP("host_to_card send failed, ret = %d\n", ret);
		goto out;
	}

    rda5890_shedule_timeout(3);   //3ms delay
	while(retry < 20)
	{
	      sdio_claim_host(card->func);
		status = sdio_readb(card->func, IF_SDIO_FUN1_INT_STAT, &ret);
            sdio_release_host(card->func);
		if (ret)
		    goto out;

		if (status & IF_SDIO_INT_AHB2SDIO)
		{
			int ret = 0;
			u8 size_l = 0, size_h = 0;
			u16 size, chunk;

                    sdio_claim_host(card->func);
			size_l = sdio_readb(card->func, IF_SDIO_AHB2SDIO_PKTLEN_L, &ret);
                   sdio_release_host(card->func);
			if (ret) {
				RDA5890_ERRP("read PKTLEN_L reg fail\n");
			goto out;
			}
			else
				RDA5890_ERRP("read PKTLEN_L reg size_l:%d \n", size_l);

                   sdio_claim_host(card->func);
			size_h = sdio_readb(card->func, IF_SDIO_AHB2SDIO_PKTLEN_H, &ret);
                    sdio_release_host(card->func);
			if (ret) {
				RDA5890_ERRP("read PKTLEN_H reg fail\n");
				goto out;
			}
			else
				RDA5890_ERRP("read PKTLEN_H reg size_h:%d\n",size_h);	

			size = (size_l | ((size_h & 0x7f) << 8)) * 4;
			if (size < 4) {
				RDA5890_ERRP("invalid packet size (%d bytes) from firmware\n", size);
				ret = -EINVAL;
				goto out;
			}

			/* alignment is handled on firmside */
			//chunk = sdio_align_size(card->func, size);
			chunk = size;

			RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_NORM,
			"if_sdio_card_to_host, size = %d, aligned size = %d\n", size, chunk);

			/* TODO: handle multiple packets here */
            sdio_claim_host(card->func);
			ret = sdio_readsb(card->func, card->buffer, IF_SDIO_FUN1_FIFO_RD, chunk);
            sdio_release_host(card->func);
			if (ret) {
				RDA5890_ERRP("sdio_readsb fail, ret = %d\n", ret);
			    goto out;
			}
#if 1
            if(priv->version == 7)
				sdio_writeb(card->func, 0x20 ,IF_SDIO_FUN1_INT_PEND, &ret);
#endif            

			/* TODO: this chunk size need to be handled here */
			{
				unsigned char rx_type;
				unsigned short rx_length;
				unsigned char msg_type;
				unsigned char *packet = (unsigned char *)card->buffer;

				RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
					"%s >>>\n", __func__);

				rx_type = (unsigned char)packet[1]&0xF0;
				rx_length = (unsigned short)(packet[0] + ((packet[1]&0x0f) << 8));

				if (rx_length > chunk) {
					RDA5890_ERRP("packet_len %d less than header specified length %d\n", 
						chunk, rx_length);
					goto out;
				}

				if( rx_type == 0x30 )
				{
					RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_NORM,
						"Message Packet type =%x \n", msg_type);
					msg_type = packet[2];

					if (msg_type == 'R')
					{		
                        packet += 2;
                        if(priv->wid_msg_id - 1 == packet[1])
                        {
                            ret = 0;
                            if(priv->wid_rsp_len > rx_length - 2)
                            {
                                priv->wid_rsp_len = rx_length - 2;
                                memcpy(priv->wid_rsp, packet, rx_length -2);
                            }
                            break;
                        }
                        else
                            RDA5890_ERRP("rda5890_wid_request_polling wid_msg_id is wrong %d %d wid=%x \n", priv->wid_msg_id -1, packet[1], (packet[4] | (packet[5] << 8)));					    
					}
				}
			}
		}
        
       rda5890_shedule_timeout(3); //3ms delay
       ret = -1;
       retry ++;
	}

    if(ret < 0 && count <= 3)
        goto re_send;
out:
    
	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s <<< wid: %x %x retry: %d count = %d \n", __func__, wid_req[4],wid_req[5], retry, count);

	return ret;
}
void rda5890_wid_response(struct rda5890_private *priv, 
		char *wid_rsp, unsigned short wid_rsp_len)
{
	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	if (!priv->wid_pending) {
		RDA5890_ERRP("no wid pending\n");
		return;
	}

	if (wid_rsp_len > priv->wid_rsp_len) {
		RDA5890_ERRP("not enough space for wid response, size = %d, buf = %d\n",
			wid_rsp_len, priv->wid_rsp_len);
		complete(&priv->wid_done); 
		return;
	}

	memcpy(priv->wid_rsp, wid_rsp, wid_rsp_len);
	priv->wid_rsp_len = wid_rsp_len;
	priv->wid_pending = 0;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);

	complete(&priv->wid_done); 
}

void rda5890_wid_status(struct rda5890_private *priv, 
		char *wid_status, unsigned short wid_status_len)
{
	char mac_status;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

#ifdef WIFI_UNLOCK_SYSTEM
    rda5990_wakeLock();
#endif

	mac_status = wid_status[7];
	if (mac_status == MAC_CONNECTED) {
		RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_CRIT,
			"MAC CONNECTED\n");
        
		priv->connect_status = MAC_CONNECTED;
		netif_carrier_on(priv->dev);
		netif_wake_queue(priv->dev);
        rda5890_indicate_connected(priv);
                
	    priv->first_init = 0;
        cancel_delayed_work(&priv->assoc_done_work);
		queue_delayed_work(priv->work_thread, &priv->assoc_done_work, 0);
        
        if(test_and_clear_bit(ASSOC_FLAG_ASSOC_RETRY, &priv->assoc_flags))
        {
            cancel_delayed_work_sync(&priv->assoc_work);
        }

        if(test_bit(ASSOC_FLAG_WLAN_CONNECTING, &priv->assoc_flags))        
            cancel_delayed_work(&priv->wlan_connect_work);
        
        set_bit(ASSOC_FLAG_WLAN_CONNECTING, &priv->assoc_flags);
		queue_delayed_work(priv->work_thread, &priv->wlan_connect_work, HZ*20);
        
	}
	else if (mac_status == MAC_DISCONNECTED) {
        
		RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_CRIT,
			"MAC DISCONNECTED\n");
	        
        if(priv->connect_status == MAC_CONNECTED)	
		    is_need_set_notch = 1;
		else
		    is_need_set_notch = 0;
        
        if(!test_bit(ASSOC_FLAG_ASSOC_RETRY, &priv->assoc_flags))
            {
        		priv->connect_status = MAC_DISCONNECTED;
                netif_stop_queue(priv->dev);
        		netif_carrier_off(priv->dev);
                if(!priv->first_init) // the first disconnect should not send to upper
        		    rda5890_indicate_disconnected(priv);
                else
                    priv->first_init = 0;
                
                if(test_bit(ASSOC_FLAG_ASSOC_START, &priv->assoc_flags))
                {
                    cancel_delayed_work(&priv->wlan_connect_work);
		            queue_delayed_work(priv->work_thread, &priv->wlan_connect_work, HZ*4);
                }
                
                clear_bit(ASSOC_FLAG_ASSOC_START, &priv->assoc_flags);
                clear_bit(ASSOC_FLAG_WLAN_CONNECTING, &priv->assoc_flags);
            }
        else
            {
                RDA5890_ERRP("********wep assoc will be retry ---------- 0x%02x\n", mac_status);
            }
	}
	else {
		RDA5890_ERRP("Invalid MAC Status 0x%02x\n", mac_status);
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
    
#ifdef WIFI_UNLOCK_SYSTEM
    rda5990_wakeUnlock();
#endif    
}

void rda5890_card_to_host(struct rda5890_private *priv, 
		char *packet, unsigned short packet_len)
{
	unsigned char rx_type;
	unsigned short rx_length;
	unsigned char msg_type;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	rx_type = (unsigned char)packet[1]&0xF0;
	rx_length = (unsigned short)(packet[0] + ((packet[1]&0x0f) << 8));

	if (rx_length > packet_len) {
		RDA5890_ERRP("packet_len %d less than header specified length %d\n", 
			packet_len, rx_length);
		goto out;
	}

	if( rx_type == 0x30 )
	{
		RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_NORM,
			"Message Packet\n");
		msg_type = packet[2];
		if(msg_type == 'I')
		{
			RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_NORM,
				"Indication Message\n");
			rda5890_wid_status(priv, packet + 2, rx_length - 2);
		}
		else if (msg_type == 'R')
		{
			RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_NORM,
				"WID Message\n");
			rda5890_wid_response(priv, packet + 2, rx_length - 2);
		}
#ifdef GET_SCAN_FROM_NETWORK_INFO
        else if(msg_type == 'N')
        {
            extern void rda5890_network_information(struct rda5890_private *priv, 
            char *info, unsigned short info_len);
            rda5890_network_information(priv, packet + 2, rx_length - 2);
        }
#endif             
		else {
			//RDA5890_ERRP("Invalid Message Type '%c'\n", msg_type);
		}
	}
	else if(rx_type == 0x20)
	{
		RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_NORM,
			"Data Packet\n");
		rda5890_data_rx(priv, packet + 2, rx_length - 2);
	}
	else {
		RDA5890_ERRP("Invalid Packet Type 0x%02x\n", rx_type);
	}

out:

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
}

/*
 * check wid response header
 * if no error, return the pointer to the payload
 * support only 1 wid per packet
 */
int rda5890_check_wid_response(char *wid_rsp, unsigned short wid_rsp_len,
		unsigned short wid, char wid_msg_id, 
		char payload_len, char **ptr_payload)
{
	unsigned short rsp_len;
	unsigned short rsp_wid;
	unsigned char msg_len;

	if (wid_rsp[0] != 'R') {
		RDA5890_ERRP("wid_rsp[0] != 'R'\n");
		goto err;
	}

	if (wid_rsp[1] != wid_msg_id) {
		RDA5890_ERRP("wid_msg_id not match msg_id: %d \n", wid_rsp[1]);
		goto err;
	}

	if (wid_rsp_len < 4) {
		RDA5890_ERRP("wid_rsp_len < 4\n");
		goto err;
	}

	rsp_len = wid_rsp[2] | (wid_rsp[3] << 8);
	if (wid_rsp_len != rsp_len) {
		RDA5890_ERRP("wid_rsp_len not match, %d != %d\n", wid_rsp_len, rsp_len);
		goto err;
	}

	if (wid_rsp_len < 7) {
		RDA5890_ERRP("wid_rsp_len < 7\n");
		goto err;
	}
#if 0
	rsp_wid = wid_rsp[4] | (wid_rsp[5] << 8);
	if (wid != rsp_wid) {
		RDA5890_ERRP("wid not match, 0x%04x != 0x%04x\n", wid, rsp_wid);
		goto err;
	}
#endif
	msg_len = wid_rsp[6];
	if (wid_rsp_len != msg_len + 7) {
		RDA5890_ERRP("msg_len not match, %d + 7 != %d\n", msg_len, wid_rsp_len);
		goto err;
	}

	if (payload_len != msg_len) {
		RDA5890_ERRP("payload_len not match, %d  != %d\n", msg_len, payload_len);
		goto err;
	}

	*ptr_payload = wid_rsp + 7;

    return 0;
    
err:
    RDA5890_ERRP("rda5890_check_wid_response failed wid=0x%04x wid_msg_id:%d \n" ,wid_rsp[4] | (wid_rsp[5] << 8), wid_msg_id);
	return -EINVAL;
}

/*
 * check wid status response
 */
int rda5890_check_wid_status(char *wid_rsp, unsigned short wid_rsp_len, 
		char wid_msg_id)
{
	int ret;
	unsigned short wid = WID_STATUS;
	char *ptr_payload;
	char status_val;

	ret = rda5890_check_wid_response(wid_rsp, wid_rsp_len, wid, wid_msg_id, 
	    1, &ptr_payload);
	if (ret) {
		RDA5890_ERRP("rda5890_check_wid_status, check_wid_response fail\n");
		return ret;
	}

	status_val = ptr_payload[0];
	if (status_val != WID_STATUS_SUCCESS) {
		RDA5890_ERRP("check_wid_status NOT success, status = %d\n", status_val);
		return -EINVAL;
	}

	return 0;
}

int rda5890_generic_get_uchar(struct rda5890_private *priv, 
		unsigned short wid, unsigned char *val)
{
	int ret;
	char wid_req[6];
	unsigned short wid_req_len = 6;
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	char wid_msg_id = priv->wid_msg_id++;
	char *ptr_payload;

	wid_req[0] = 'Q';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_response(wid_rsp, wid_rsp_len, wid, wid_msg_id, 
		sizeof(unsigned char), &ptr_payload);
	if (ret) {
		RDA5890_ERRP("check_wid_response fail, ret = %d\n", ret);
		goto out;
	}

	*val = *ptr_payload;
out:
	return ret;
}

int rda5890_generic_set_uchar(struct rda5890_private *priv, 
		unsigned short wid, unsigned char val)
{
	int ret;
	char wid_req[7 + sizeof(unsigned char)];
	unsigned short wid_req_len = 7 + 1;
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	char wid_msg_id = priv->wid_msg_id++;

	wid_req[0] = 'W';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	wid_req[6] = 1;
	wid_req[7] = val;
	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_status(wid_rsp, wid_rsp_len, wid_msg_id);
	if (ret) {
		RDA5890_ERRP("check_wid_status fail, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}

#if 1
int rda5890_generic_get_ushort(struct rda5890_private *priv, 
		unsigned short wid, unsigned short *val)
{
	int ret;
	char wid_req[6];
	unsigned short wid_req_len = 6;
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	char wid_msg_id = priv->wid_msg_id++;
	char *ptr_payload;

	wid_req[0] = 'Q';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_response(wid_rsp, wid_rsp_len, wid, wid_msg_id, 
		sizeof(unsigned short), &ptr_payload);
	if (ret) {
		RDA5890_ERRP("check_wid_response fail, ret = %d\n", ret);
		goto out;
	}

	memcpy(val, ptr_payload, sizeof(unsigned short)); 

out:
	return ret;
}

int rda5890_generic_set_ushort(struct rda5890_private *priv, 
		unsigned short wid, unsigned short val)
{
	int ret;
	char wid_req[7 + sizeof(unsigned short)];
	unsigned short wid_req_len = 7 + sizeof(unsigned short);
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	char wid_msg_id = priv->wid_msg_id++;

	wid_req[0] = 'W';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	wid_req[6] = (char)(sizeof(unsigned short));
	memcpy(wid_req + 7, &val, sizeof(unsigned short)); 

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_status(wid_rsp, wid_rsp_len, wid_msg_id);
	if (ret) {
		RDA5890_ERRP("check_wid_status fail, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}
#endif

int rda5890_generic_get_ulong(struct rda5890_private *priv, 
		unsigned short wid, unsigned long *val)
{
	int ret;
	char wid_req[6];
	unsigned short wid_req_len = 6;
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	char wid_msg_id = priv->wid_msg_id++;
	char *ptr_payload;

	wid_req[0] = 'Q';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_response(wid_rsp, wid_rsp_len, wid, wid_msg_id, 
		sizeof(unsigned long), &ptr_payload);
	if (ret) {
		RDA5890_ERRP("check_wid_response fail, ret = %d\n", ret);
		goto out;
	}

	memcpy(val, ptr_payload, sizeof(unsigned long)); 

out:
	return ret;
}

int rda5890_generic_set_ulong(struct rda5890_private *priv, 
		unsigned short wid, unsigned long val)
{
	int ret;
	char wid_req[7 + sizeof(unsigned long)];
	unsigned short wid_req_len = 7 + sizeof(unsigned long);
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	char wid_msg_id = priv->wid_msg_id++;

	wid_req[0] = 'W';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	wid_req[6] = (char)(sizeof(unsigned long));
	memcpy(wid_req + 7, &val, sizeof(unsigned long)); 

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_status(wid_rsp, wid_rsp_len, wid_msg_id);
	if (ret) {
		RDA5890_ERRP("check_wid_status fail, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}

int rda5890_generic_get_str(struct rda5890_private *priv, 
		unsigned short wid, unsigned char *val, unsigned char len)
{
	int ret;
	char wid_req[6];
	unsigned short wid_req_len = 6;
	char wid_rsp[RDA5890_MAX_WID_LEN];
	unsigned short wid_rsp_len = RDA5890_MAX_WID_LEN;
	char wid_msg_id = priv->wid_msg_id++;
	char *ptr_payload;

	wid_req[0] = 'Q';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_response(wid_rsp, wid_rsp_len, wid, wid_msg_id, 
		(char)len, &ptr_payload);
	if (ret) {
		RDA5890_ERRP("check_wid_response fail, ret = %d\n", ret);
		goto out;
	}

	memcpy(val, ptr_payload, len); 

out:
	return ret;
}

int rda5890_generic_set_str(struct rda5890_private *priv, 
		unsigned short wid, unsigned char *val, unsigned char len)
{
	int ret;
	char wid_req[RDA5890_MAX_WID_LEN];
	unsigned short wid_req_len = 7 + len;
	char wid_rsp[RDA5890_MAX_WID_LEN];
	unsigned short wid_rsp_len = RDA5890_MAX_WID_LEN;
	char wid_msg_id = priv->wid_msg_id++;

	wid_req[0] = 'W';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	wid_req[6] = (char)(len);
	memcpy(wid_req + 7, val, len); 

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_status(wid_rsp, wid_rsp_len, wid_msg_id);
	if (ret) {
		RDA5890_ERRP("check_wid_status fail, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}

int rda5890_check_wid_response_unknown_len(
        char *wid_rsp, unsigned short wid_rsp_len,
		unsigned short wid, char wid_msg_id, 
		char *payload_len, char **ptr_payload)
{
	unsigned short rsp_len;
	unsigned short rsp_wid;
	unsigned char msg_len;

	if (wid_rsp[0] != 'R') {
		RDA5890_ERRP("wid_rsp[0] != 'R'\n");
		return -EINVAL;
	}

	if (wid_rsp[1] != wid_msg_id) {
		RDA5890_ERRP("wid_msg_id not match\n");
		return -EINVAL;
	}

	if (wid_rsp_len < 4) {
		RDA5890_ERRP("wid_rsp_len < 4\n");
		return -EINVAL;
	}

	rsp_len = wid_rsp[2] | (wid_rsp[3] << 8);
	if (wid_rsp_len != rsp_len) {
		RDA5890_ERRP("wid_rsp_len not match, %d != %d\n", wid_rsp_len, rsp_len);
		return -EINVAL;
	}

	if (wid_rsp_len < 7) {
		RDA5890_ERRP("wid_rsp_len < 7\n");
		return -EINVAL;
	}
#if 0
	rsp_wid = wid_rsp[4] | (wid_rsp[5] << 8);
	if (wid != rsp_wid) {
		RDA5890_ERRP("wid not match, 0x%04x != 0x%04x\n", wid, rsp_wid);
		return -EINVAL;
	}
#endif
	msg_len = wid_rsp[6];
	if (wid_rsp_len != msg_len + 7) {
		RDA5890_ERRP("msg_len not match, %d + 7 != %d\n", msg_len, wid_rsp_len);
		return -EINVAL;
	}

	*payload_len = msg_len;

	*ptr_payload = wid_rsp + 7;

	return 0;
err:

    RDA5890_ERRP("wid is %x wid_msg %d \n", wid_rsp[4] | (wid_rsp[5] << 8), wid_rsp[1]);
    return -EINVAL;
}

int rda5890_get_str_unknown_len(struct rda5890_private *priv, 
		unsigned short wid, unsigned char *val, unsigned char *len)
{
	int ret;
	char wid_req[6];
	unsigned short wid_req_len = 6;
	char wid_rsp[RDA5890_MAX_WID_LEN];
	unsigned short wid_rsp_len = RDA5890_MAX_WID_LEN;
	char wid_msg_id = priv->wid_msg_id++;
	char *ptr_payload;

	wid_req[0] = 'Q';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_response_unknown_len(
	    wid_rsp, wid_rsp_len, wid, wid_msg_id, (char*)len, &ptr_payload);
	if (ret) {
		RDA5890_ERRP("check_wid_response fail, ret = %d\n", ret);
		goto out;
	}

	memcpy(val, ptr_payload, *len); 

out:
	return ret;
}

extern int rda5890_sdio_set_default_notch(struct rda5890_private *priv);
int rda5890_start_scan(struct rda5890_private *priv)
{
	int ret;
	char wid_req[12];
	unsigned short wid_req_len = 12;
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	unsigned short wid;
	char wid_msg_id = 0;

	if(priv->connect_status != MAC_CONNECTED)
    {
        rda5890_sdio_set_default_notch(priv);
    }

    wid_msg_id = priv->wid_msg_id++;
    
	wid_req[0] = 'W';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid = WID_SITE_SURVEY;
	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	wid_req[6] = (char)(0x01);
	wid_req[7] = (char)(0x01);

	wid = WID_START_SCAN_REQ;
	wid_req[8] = (char)(wid&0x00FF);
	wid_req[9] = (char)((wid&0xFF00) >> 8);

	wid_req[10] = (char)(0x01);
	wid_req[11] = (char)(0x01);
    

    wid_req_len = 12;
	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);
    
	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_status(wid_rsp, wid_rsp_len, wid_msg_id);
	if (ret) {
		RDA5890_ERRP("check_wid_status fail, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}


int rda5890_start_scan_enable_network_info(struct rda5890_private *priv)
{
	int ret;
	char wid_req[12];
	unsigned short wid_req_len = 12;
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	unsigned short wid;
	char wid_msg_id = 0;

	if(priv->connect_status != MAC_CONNECTED)
    {
        rda5890_sdio_set_default_notch(priv);
    }

    wid_msg_id = priv->wid_msg_id++;
    
	wid_req[0] = 'W';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid = WID_SITE_SURVEY;
	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);

	wid_req[6] = (char)(0x01);
	wid_req[7] = (char)(0x01);

	wid = WID_START_SCAN_REQ;
	wid_req[8] = (char)(wid&0x00FF);
	wid_req[9] = (char)((wid&0xFF00) >> 8);

	wid_req[10] = (char)(0x01);
	wid_req[11] = (char)(0x01);
    
    wid = WID_NETWORK_INFO_EN;
	wid_req[12] = (char)(wid&0x00FF);
	wid_req[13] = (char)((wid&0xFF00) >> 8);

	wid_req[14] = (char)(0x01);
	wid_req[15] = (char)(0x01); // 0x01 scan network info
	
	wid_req_len = 16;
	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_wid_request fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_status(wid_rsp, wid_rsp_len, wid_msg_id);
	if (ret) {
		RDA5890_ERRP("check_wid_status fail, ret = %d\n", ret);
		goto out;
	}

out:
	return ret;
}

int rda5890_start_join(struct rda5890_private *priv)
{
    int ret;
	char wid_req[255];
	unsigned short wid_req_len = 0;
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	unsigned short wid;
	char wid_msg_id = priv->wid_msg_id++;
    unsigned short i = 0;
    unsigned char * wep_key = 0, key_str_len = 0;
    unsigned char key_str[26 + 1] , * key, *pWid_req;
    
    RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
        "%s <<< mode:%d authtype:%d ssid:%s\n", __func__, priv->imode, priv->authtype,
        priv->assoc_ssid);
    
	wid_req[0] = 'W';
	wid_req[1] = wid_msg_id;


    wid = WID_802_11I_MODE;
	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);
    
	wid_req[6] = 1;
    wid_req[7] = priv->imode;

    wid = WID_AUTH_TYPE;
    wid_req[8] = (char)(wid&0x00FF);
	wid_req[9] = (char)((wid&0xFF00) >> 8);
    
	wid_req[10] = 1;
    wid_req[11] = priv->authtype;
    
#ifdef GET_SCAN_FROM_NETWORK_INFO
    wid = WID_NETWORK_INFO_EN;
    wid_req[12] = (char)(wid&0x00FF);
	wid_req[13] = (char)((wid&0xFF00) >> 8);
    
	wid_req[14] = 1;
    wid_req[15] = 0;

    wid = WID_CURRENT_TX_RATE;
    wid_req[16] = (char)(wid&0x00FF);
	wid_req[17] = (char)((wid&0xFF00) >> 8);
    
	wid_req[18] = 1;
    wid_req[19] = 1;
    
    wid_req_len = 20;
    pWid_req = wid_req + 20;    
#else
    wid_req_len = 12;
    pWid_req = wid_req + 12;
#endif
    wid = WID_WEP_KEY_VALUE0;
    if(priv->imode == 3 || priv->imode == 7) //write wep key
    {
        for(i = 0 ; i < 4; i ++)
        {
            key = priv->wep_keys[i].key;

            if(priv->wep_keys[i].len == 0)
                continue;
            
            if (priv->wep_keys[i].len == KEY_LEN_WEP_40) {
                sprintf(key_str, "%02x%02x%02x%02x%02x\n", 
                key[0], key[1], key[2], key[3], key[4]);
                key_str_len = 10;
                key_str[key_str_len] = '\0';
            }
            else if (priv->wep_keys[i].len == KEY_LEN_WEP_104) {
                sprintf(key_str, "%02x%02x%02x%02x%02x"
                "%02x%02x%02x%02x%02x"
                "%02x%02x%02x\n",
                key[0], key[1], key[2], key[3], key[4],
                key[5], key[6], key[7], key[8],	key[9],
                key[10], key[11], key[12]);
                key_str_len = 26;
                key_str[key_str_len] = '\0';
            }
            else
                continue;

            pWid_req[0] = (char)((wid + i)&0x00FF);
	        pWid_req[1] = (char)(((wid + i)&0xFF00) >> 8);
    
	        pWid_req[2] = key_str_len;
            memcpy(pWid_req + 3, key_str, key_str_len);
            
            pWid_req += 3 + key_str_len;
            wid_req_len += 3 + key_str_len;            
        }
    }

//  ssid
    wid = WID_SSID;
    pWid_req[0] = (char)(wid&0x00FF);
	pWid_req[1] = (char)((wid&0xFF00) >> 8);
    
	pWid_req[2] = priv->assoc_ssid_len;
    memcpy(pWid_req + 3, priv->assoc_ssid, priv->assoc_ssid_len);
    
    wid_req_len += 3 + priv->assoc_ssid_len;

    
	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_start_join fail, ret = %d\n", ret);
		goto out;
	}

	ret = rda5890_check_wid_status(wid_rsp, wid_rsp_len, wid_msg_id);
	if (ret) {
		RDA5890_ERRP("rda5890_start_join check status fail, ret = %d\n", ret);
		goto out;
	}

out:
    
    RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
        "%s >>> ret = %d req len %d mod:0x%x auth_type:0x%x \n", __func__, ret, wid_req_len, priv->imode
        , priv->authtype);
	return ret;    
}

int rda5890_set_txrate(struct rda5890_private *priv, unsigned char mbps)
{
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"rda5890_set_txrate <<< \n");

	ret = rda5890_generic_set_uchar(priv, WID_CURRENT_TX_RATE, mbps); //O FOR AUTO 1FOR 1MBPS
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"rda5890_set_txrate success >>> \n");

out:
	return ret;
}

int rda5890_set_core_init_polling(struct rda5890_private *priv, const unsigned int (*data)[2], unsigned char num)
{
        int ret = -1;
        char wid_req[255];
        unsigned short wid_req_len = 4 + 14*num;
        char wid_rsp[32];
        unsigned short wid_rsp_len = 32;
        unsigned short wid;
        char wid_msg_id = priv->wid_msg_id++;
	    char count = 0, *p_wid_req = NULL;

    	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s <<< \n", __func__);

        wid_req[0] = 'W';
        wid_req[1] = wid_msg_id;

        wid_req[2] = (char)(wid_req_len&0x00FF);
        wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

        p_wid_req = wid_req + 4;
        for(count = 0; count < num; count ++)
        {
            wid = WID_MEMORY_ADDRESS;
            p_wid_req[0] = (char)(wid&0x00FF);
            p_wid_req[1] = (char)((wid&0xFF00) >> 8);

            p_wid_req[2] = (char)4;
            memcpy(p_wid_req + 3, (char*)&data[count][0], 4);

            wid = WID_MEMORY_ACCESS_32BIT;
            p_wid_req[7] = (char)(wid&0x00FF);
            p_wid_req[8] = (char)((wid&0xFF00) >> 8);

            p_wid_req[9] = (char)4;
            memcpy(p_wid_req + 10, (char*)&data[count][1], 4);
            p_wid_req += 14;
        }
        ret = rda5890_wid_request_polling(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
     	if (ret) {
                RDA5890_ERRP("rda5890_set_sdio_core_init fail, ret = %d\n", ret);
                goto out;
        }

out:
        RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s >>> \n", __func__);
        return ret;
}

int rda5890_set_core_init(struct rda5890_private *priv, const unsigned int (*data)[2], unsigned char num)
{
        int ret = -1;
        char wid_req[255];
        unsigned short wid_req_len = 4 + 14*num;
        char wid_rsp[32];
        unsigned short wid_rsp_len = 32;
        unsigned short wid;
        char wid_msg_id = priv->wid_msg_id++;
        char count = 0, *p_wid_req = NULL;

        wid_req[0] = 'W';
        wid_req[1] = wid_msg_id;

        wid_req[2] = (char)(wid_req_len&0x00FF);
        wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

        p_wid_req = wid_req + 4;
        for(count = 0; count < num; count ++)
        {
            wid = WID_MEMORY_ADDRESS;
            p_wid_req[0] = (char)(wid&0x00FF);
            p_wid_req[1] = (char)((wid&0xFF00) >> 8);

            p_wid_req[2] = (char)4;
            memcpy(p_wid_req + 3, (char*)&data[count][0], 4);

            wid = WID_MEMORY_ACCESS_32BIT;
            p_wid_req[7] = (char)(wid&0x00FF);
            p_wid_req[8] = (char)((wid&0xFF00) >> 8);

            p_wid_req[9] = (char)4;
            memcpy(p_wid_req + 10, (char*)&data[count][1], 4);
            p_wid_req += 14;
        }
        ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
     	if (ret) {
                RDA5890_ERRP("rda5890_set_sdio_core_init fail, ret = %d\n", ret);
                goto out;
        }

out:
        return ret;
}

int rda5890_set_core_patch_polling(struct rda5890_private *priv, const unsigned char (*patch)[2], unsigned char num)
{
        int ret;
        char wid_req[255];
        unsigned short wid_req_len = 4 + 8*num;
        char wid_rsp[32];
        unsigned short wid_rsp_len = 32;
        unsigned short wid;
        char wid_msg_id = priv->wid_msg_id++;
        char count = 0 , *p_wid_req = NULL;	
        
    	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s <<< \n", __func__);	
        
        wid_req[0] = 'W';
        wid_req[1] = wid_msg_id;

        wid_req[2] = (char)(wid_req_len&0x00FF);
        wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

    	p_wid_req = wid_req + 4;
    	for(count = 0; count < num; count ++)
        {
            wid = WID_PHY_ACTIVE_REG;
            p_wid_req[0] = (char)(wid&0x00FF);
            p_wid_req[1] = (char)((wid&0xFF00) >> 8);

            p_wid_req[2] = (char)(0x01);
            p_wid_req[3] = patch[count][0];

            wid = WID_PHY_ACTIVE_REG_VAL;
            p_wid_req[4] = (char)(wid&0x00FF);
            p_wid_req[5] = (char)((wid&0xFF00) >> 8);

            p_wid_req[6] = (char)(0x01);
            p_wid_req[7] = patch[count][1];
            p_wid_req += 8;
        }
        ret = rda5890_wid_request_polling(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
        if (ret) {
                RDA5890_ERRP("rda5890_set_core_patch fail, ret = %d \n", ret);
                goto out;
        }

out:
        RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s >>> \n", __func__);	
        return ret;
}

int rda5890_set_core_patch(struct rda5890_private *priv, const unsigned char (*patch)[2], unsigned char num)
{
        int ret;
        char wid_req[255];
        unsigned short wid_req_len = 4 + 8*num;
        char wid_rsp[32];
        unsigned short wid_rsp_len = 32;
        unsigned short wid;
        char wid_msg_id = priv->wid_msg_id++;
        char count = 0 , *p_wid_req = NULL;	
	
        wid_req[0] = 'W';
        wid_req[1] = wid_msg_id;

        wid_req[2] = (char)(wid_req_len&0x00FF);
        wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	    p_wid_req = wid_req + 4;
        for(count = 0; count < num; count ++)
        {
            wid = WID_PHY_ACTIVE_REG;
            p_wid_req[0] = (char)(wid&0x00FF);
            p_wid_req[1] = (char)((wid&0xFF00) >> 8);

            p_wid_req[2] = (char)(0x01);
            p_wid_req[3] = patch[count][0];

            wid = WID_PHY_ACTIVE_REG_VAL;
            p_wid_req[4] = (char)(wid&0x00FF);
            p_wid_req[5] = (char)((wid&0xFF00) >> 8);

            p_wid_req[6] = (char)(0x01);
            p_wid_req[7] = patch[count][1];
            p_wid_req += 8;
        }
        ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
        if (ret) {
                RDA5890_ERRP("rda5890_set_core_patch fail, ret = %d \n", ret);
                goto out;
        }

out:
        return ret;
}


int rda5890_get_fw_ver(struct rda5890_private *priv, unsigned long *fw_ver)
{
	int ret;

	ret = rda5890_generic_get_ulong(priv, WID_SYS_FW_VER, fw_ver);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Get FW_VER 0x%08lx\n", *fw_ver);

out:
	return ret;
}

int rda5890_get_mac_addr(struct rda5890_private *priv, unsigned char *mac_addr)
{
	int ret;

	ret = rda5890_generic_get_str(priv, WID_MAC_ADDR, mac_addr, ETH_ALEN);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"STA MAC Address [%02x:%02x:%02x:%02x:%02x:%02x]\n",
		mac_addr[0], mac_addr[1], mac_addr[2],
		mac_addr[3], mac_addr[4], mac_addr[5]);
out:
	return ret;
}

/* support only one bss per packet for now */
int rda5890_get_scan_results(struct rda5890_private *priv, 
		struct rda5890_bss_descriptor *bss_desc)
{
	int ret;
	int count = 0;
	unsigned char len;
	unsigned char buf[sizeof(struct rda5890_bss_descriptor)*RDA5890_MAX_NETWORK_NUM + 2];
    unsigned char first_send = 0;

	ret = rda5890_get_str_unknown_len(priv, WID_SITE_SURVEY_RESULTS,
		buf, &len);
	if (ret) {
		return ret;
	}
	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Get Scan Result, len = %d\n", len);

	if ((len - 2) % sizeof(struct rda5890_bss_descriptor)) {
		RDA5890_ERRP("Scan Result len not correct, %d\n", len);
		return -EINVAL;
	}

	count = (len - 2) / sizeof(struct rda5890_bss_descriptor);

    first_send = *(buf + 1);
	memcpy(bss_desc, buf + 2, (len - 2)); 

	return (count | first_send << 8);
}

int rda5890_get_bssid(struct rda5890_private *priv, unsigned char *bssid)
{
	int ret;

	ret = rda5890_generic_get_str(priv, WID_BSSID, bssid, ETH_ALEN);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Get BSSID [%02x:%02x:%02x:%02x:%02x:%02x]\n",
		bssid[0], bssid[1], bssid[2],
		bssid[3], bssid[4], bssid[5]);
out:
	return ret;
}

int rda5890_get_channel(struct rda5890_private *priv, unsigned char *channel)
{
	int ret;

	ret = rda5890_generic_get_uchar(priv, WID_CURRENT_CHANNEL, channel);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Get Channel %d\n", *channel);

out:
	return ret;
}

int rda5890_get_rssi(struct rda5890_private *priv, unsigned char *rssi)
{
	int ret;

    *rssi = 0;
	ret = rda5890_generic_get_uchar(priv, WID_RSSI, rssi);
	if (ret) {
		goto out;
	}


	if(priv->connect_status == MAC_CONNECTED)
	{
		if(*rssi > 215)
		{
			rda5890_rssi_up_to_200(priv);			
		}
		else
		{
			rda5890_sdio_set_notch_by_channel(priv, priv->curbssparams.channel);			
		}
	}
    else
        *rssi = 0;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Get RSSI <<< %d\n", *(char*)rssi);

out:
	return ret;
}

int rda5890_set_mac_addr(struct rda5890_private *priv, unsigned char *mac_addr)
{
	int ret;

	ret = rda5890_generic_set_str(priv, WID_MAC_ADDR, mac_addr, ETH_ALEN);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set STA MAC Address [%02x:%02x:%02x:%02x:%02x:%02x]\n",
		mac_addr[0], mac_addr[1], mac_addr[2],
		mac_addr[3], mac_addr[4], mac_addr[5]);
out:
	return ret;
}

int rda5890_set_preamble(struct rda5890_private *priv, unsigned char  preamble)
{
	int ret;

	ret = rda5890_generic_set_uchar(priv, WID_PREAMBLE, preamble);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE, "rda5890_set_preamble \n");
out:
	return ret;
}

#ifdef GET_SCAN_FROM_NETWORK_INFO

int rda5890_set_scan_complete(struct rda5890_private *priv)
{
	int ret;

	ret = rda5890_generic_set_uchar(priv, WID_NETWORK_INFO_EN, 0);
	if (ret) {
		goto out;
	}
     
out:
      RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE, "rda5890_set_scan_complete  ret=%d \n", ret);
	return ret;
}
#endif

int rda5890_set_ssid(struct rda5890_private *priv, 
		unsigned char *ssid, unsigned char ssid_len)
{
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set SSID: %s, len = %d\n", ssid, ssid_len);

	ret = rda5890_generic_set_str(priv, WID_SSID, ssid, ssid_len);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set SSID Done\n");

out:
	return ret;
}

int rda5890_get_ssid(struct rda5890_private *priv, 
		unsigned char *ssid, unsigned char *ssid_len)
{
    int ret;
    
	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Get SSID \n");
    
    ret = rda5890_get_str_unknown_len(priv, WID_SSID, ssid, ssid_len);
    if(*ssid_len > 0)
        ssid[*ssid_len] = '\0';
    
    RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
	"Get SSID Done len:%d %s\n", *ssid_len, *ssid_len > 0? ssid:"NULL");
out:
	return ret;
}
int rda5890_set_bssid(struct rda5890_private *priv, unsigned char *bssid)
{
	int ret;

	ret = rda5890_generic_set_str(priv, WID_BSSID, bssid, ETH_ALEN);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set BSSID [%02x:%02x:%02x:%02x:%02x:%02x]\n",
		bssid[0], bssid[1], bssid[2],
		bssid[3], bssid[4], bssid[5]);
out:
	return ret;
}


int rda5890_set_imode(struct rda5890_private *priv, unsigned char imode)
{
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set IMode 0x%02x\n", imode);

	ret = rda5890_generic_set_uchar(priv, WID_802_11I_MODE, imode);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set IMode Done\n");

out:
	return ret;
}

int rda5890_set_authtype(struct rda5890_private *priv, unsigned char authtype)
{
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set AuthType 0x%02x\n", authtype);

	ret = rda5890_generic_set_uchar(priv, WID_AUTH_TYPE, authtype);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set AuthType Done\n");

out:
	return ret;
}

int rda5890_set_listen_interval(struct rda5890_private *priv, unsigned char interval)
{
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set rda5890_set_listen_interval 0x%02x\n", interval);

	ret = rda5890_generic_set_uchar(priv, WID_LISTEN_INTERVAL, interval);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set rda5890_set_listen_interval Done\n");

out:
	return ret;
}

int rda5890_set_link_loss_threshold(struct rda5890_private *priv, unsigned char threshold)
{
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set rda5890_set_link_loss_threshold 0x%02x\n", threshold);

	ret = rda5890_generic_set_uchar(priv, WID_LINK_LOSS_THRESHOLD, threshold);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set rda5890_set_link_loss_threshold Done\n");

out:
	return ret;
}


int rda5890_set_wepkey(struct rda5890_private *priv, 
		unsigned short index, unsigned char *key, unsigned char key_len)
{
	int ret;
	unsigned char key_str[26 + 1]; // plus 1 for debug print
	unsigned char key_str_len;

	if (key_len == KEY_LEN_WEP_40) {
		sprintf(key_str, "%02x%02x%02x%02x%02x\n", 
			key[0], key[1], key[2], key[3], key[4]);
		key_str_len = 10;
		key_str[key_str_len] = '\0';
	}
	else if (key_len == KEY_LEN_WEP_104) {
		sprintf(key_str, "%02x%02x%02x%02x%02x"
			"%02x%02x%02x%02x%02x"
			"%02x%02x%02x\n",
			key[0], key[1], key[2], key[3], key[4],
			key[5], key[6], key[7], key[8],	key[9],
			key[10], key[11], key[12]);
		key_str_len = 26;
		key_str[key_str_len] = '\0';
	}
	else {
		RDA5890_ERRP("Error in WEP Key length %d\n", key_len);
		ret = -EINVAL;
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set WEP KEY[%d]: %s\n", index, key_str);

	ret = rda5890_generic_set_str(priv,
		(WID_WEP_KEY_VALUE0 + index), key_str, key_str_len);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set WEP KEY[%d] Done\n", index);

out:
	return ret;
}

static void dump_key(unsigned char *key, unsigned char key_len)
{
	RDA5890_DBGP("%02x %02x %02x %02x  %02x %02x %02x %02x\n",
		key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
	RDA5890_DBGP("%02x %02x %02x %02x  %02x %02x %02x %02x\n",
		key[8], key[9], key[10], key[11], key[12], key[13], key[14], key[15]);
	if (key_len > 16)
		RDA5890_DBGP("%02x %02x %02x %02x  %02x %02x %02x %02x\n",
			key[16], key[17], key[18], key[19], key[20], key[21], key[22], key[23]);
	if (key_len > 24)
		RDA5890_DBGP("%02x %02x %02x %02x  %02x %02x %02x %02x\n",
			key[24], key[25], key[26], key[27], key[28], key[29], key[30], key[31]);
}

int rda5890_set_ptk(struct rda5890_private *priv, 
		unsigned char *key, unsigned char key_len)
{
	int ret;
	unsigned char key_str[32 + ETH_ALEN + 1]; 
	unsigned char key_str_len = key_len + ETH_ALEN + 1;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set PTK: len = %d\n", key_len);
	if (RDA5890_DBGLA(RDA5890_DA_WID, RDA5890_DL_VERB))
		dump_key(key, key_len);

	if (priv->connect_status != MAC_CONNECTED) {
		RDA5890_ERRP("Adding PTK while not connected\n");
		ret = -EINVAL;
		goto out;
	}

	/*----------------------------------------*/
	/*    STA Addr  | KeyLength |   Key       */
	/*----------------------------------------*/
	/*       6      |     1     |  KeyLength  */
	/*----------------------------------------*/

	/*---------------------------------------------------------*/
	/*                      key                                */
	/*---------------------------------------------------------*/
	/* Temporal Key    | Rx Micheal Key    |   Tx Micheal Key  */
	/*---------------------------------------------------------*/
	/*    16 bytes     |      8 bytes      |       8 bytes     */
	/*---------------------------------------------------------*/

	memcpy(key_str, priv->curbssparams.bssid, ETH_ALEN);
	key_str[6] = key_len;
	memcpy(key_str + 7, key, 16);

	/* swap TX MIC and RX MIC, rda5890 need RX MIC to be ahead */
	if(key_len > 16) {
		memcpy(key_str + 7 + 16, key + 24, 8);
		memcpy(key_str + 7 + 24, key + 16, 8);
	}

	if(priv->is_wapi)
		ret = rda5890_generic_set_str(priv,
			WID_ADD_WAPI_PTK, key_str, key_str_len);
	else
		ret = rda5890_generic_set_str(priv,
			WID_ADD_PTK, key_str, key_str_len);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set PTK Done\n");

out:
	return ret;
}

int rda5890_set_gtk(struct rda5890_private *priv, unsigned char key_id,
		unsigned char *key_rsc, unsigned char key_rsc_len,
		unsigned char *key, unsigned char key_len)
{
	int ret;
	unsigned char key_str[32 + ETH_ALEN + 8 + 2]; 
	unsigned char key_str_len = key_len + ETH_ALEN + 8 + 2;

	/*---------------------------------------------------------*/
	/*    STA Addr  | KeyRSC | KeyID | KeyLength |   Key       */
	/*---------------------------------------------------------*/
	/*       6      |   8    |   1   |     1     |  KeyLength  */
	/*---------------------------------------------------------*/

	/*-------------------------------------*/
	/*                      key            */
	/*-------------------------------------*/
	/* Temporal Key    | Rx Micheal Key    */
	/*-------------------------------------*/
	/*    16 bytes     |      8 bytes      */
	/*-------------------------------------*/

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set GTK: len = %d\n", key_len);
	if (RDA5890_DBGLA(RDA5890_DA_WID, RDA5890_DL_VERB))
		dump_key(key, key_len);

	if (priv->connect_status != MAC_CONNECTED) {
		RDA5890_ERRP("Adding PTK while not connected\n");
		ret = -EINVAL;
		goto out;
	}

	memcpy(key_str, priv->curbssparams.bssid, ETH_ALEN);
	memcpy(key_str + 6, key_rsc, key_rsc_len);
	key_str[14] = key_id;
	key_str[15] = key_len;
	memcpy(key_str + 16, key, 16);

	/* swap TX MIC and RX MIC, rda5890 need RX MIC to be ahead */
	if(key_len > 16) {
		//memcpy(key_str + 16 + 16, key + 16, key_len - 16);
		memcpy(key_str + 16 + 16, key + 24, 8);
		memcpy(key_str + 16 + 24, key + 16, 8);
	}

	if(priv->is_wapi)
		ret = rda5890_generic_set_str(priv,
			WID_ADD_WAPI_RX_GTK, key_str, key_str_len);
	else
		ret = rda5890_generic_set_str(priv,
			WID_ADD_RX_GTK, key_str, key_str_len);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set GTK Done\n");

out:
	return ret;
}

int rda5890_set_scan_timeout(struct rda5890_private *priv)
{
	int ret;
	char wid_req[14];
	unsigned short wid_req_len = 19;
	char wid_rsp[32];
	unsigned short wid_rsp_len = 32;
	char wid_msg_id = priv->wid_msg_id++;
	char *ptr_payload;
    unsigned short wid = 0;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
	"rda5890_set_scan_timeout <<< \n");

    wid = WID_SITE_SURVEY_SCAN_TIME;
    
	wid_req[0] = 'W';
	wid_req[1] = wid_msg_id;

	wid_req[2] = (char)(wid_req_len&0x00FF);
	wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

	wid_req[4] = (char)(wid&0x00FF);
	wid_req[5] = (char)((wid&0xFF00) >> 8);
    wid_req[6] = 2;
    wid_req[7] = 50;    //50 ms one channel
    wid_req[8] = 0;

    wid = WID_ACTIVE_SCAN_TIME;
    wid_req[9] = (char)(wid&0x00FF);
	wid_req[10] = (char)((wid&0xFF00) >> 8);
    wid_req[11] = 2;
    wid_req[12] = 50;   //50 ms one channel
    wid_req[13] = 0;

    wid = WID_PASSIVE_SCAN_TIME;
    wid_req[14] = (char)(wid&0x00FF);
	wid_req[15] = (char)((wid&0xFF00) >> 8);
    wid_req[16] = 2;
    wid_req[17] = 50;   //50 ms one channel
    wid_req[18] = 0;

	ret = rda5890_wid_request(priv, wid_req, wid_req_len, wid_rsp, &wid_rsp_len);
	if (ret) {
		RDA5890_ERRP("rda5890_set_scan_timeout fail, ret = %d\n", ret);
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"rda5890_set_scan_timeout >>> \n");

out:
	return ret;
}


int rda5890_set_pm_mode(struct rda5890_private *priv, unsigned char pm_mode)
{
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set PM Mode 0x%02x\n", pm_mode);

	ret = rda5890_generic_set_uchar(priv, WID_POWER_MANAGEMENT, pm_mode);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set PM Mode Done\n");

out:
	return ret;
}

int rda5890_set_preasso_sleep(struct rda5890_private *priv, unsigned int preasso_sleep)
{
	int ret;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set Preasso Sleep 0x%08x\n", preasso_sleep);

	ret = rda5890_generic_set_ulong(priv, WID_PREASSO_SLEEP, preasso_sleep);
	if (ret) {
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Set Preasso Sleep Done\n");

out:
	return ret;
}

