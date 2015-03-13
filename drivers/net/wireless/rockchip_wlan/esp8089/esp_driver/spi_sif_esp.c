/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *   spi serial i/f driver
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
#include "esp_file.h"
#ifdef USE_EXT_GPIO
#include "esp_ext.h"
#endif /* USE_EXT_GPIO */


#ifdef ESP_PREALLOC
extern u8 *esp_get_lspi_buf(void);
extern void esp_put_lspi_buf(u8 **p);
#endif

#define SPI_BLOCK_SIZE              (512)

#define MAX_BUF_SIZE        (48*1024)

static unsigned char *buf_addr = NULL;
static unsigned char *tx_cmd;
static unsigned char *rx_cmd;
static unsigned char *check_buf = NULL;
static unsigned char *ff_buf = NULL;

unsigned int crc_ta_8[256]={ 
                                0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
                                0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
                                0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
                                0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
                                0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
                                0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
                                0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
                                0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
                                0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
                                0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
                                0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
                                0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
                                0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
                                0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
                                0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
                                0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
                                0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
                                0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
                                0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
                                0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
                                0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
                                0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
                                0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
                                0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
                                0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
                                0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
                                0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
                                0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
                                0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
                                0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
                                0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
                                0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
                                };


unsigned int crc_cal_by_byte(unsigned char* ptr, int len)
{
    unsigned short crc = 0;

    while(len-- != 0)
    {    
        unsigned int high = (unsigned int)(crc >> 8);
        crc <<= 8;
        crc ^= crc_ta_8[high^*ptr];
        ptr++;
    }

    return crc;
}

struct task_struct *sif_irq_thread;

#define ESP_DMA_IBUFSZ   2048

//unsigned int esp_msg_level = 0;
unsigned int esp_msg_level = ESP_DBG_ERROR | ESP_SHOW;

static struct semaphore esp_powerup_sem;

static enum esp_sdio_state sif_sdio_state;
struct esp_spi_ctrl *sif_sctrl = NULL;
static struct esp_spi_resp spi_resp;

#ifdef ESP_ANDROID_LOGGER
bool log_off = false;
#endif /* ESP_ANDROID_LOGGER */

#ifdef REQUEST_RTC_IRQ
extern int request_rtc_irq(void);
#endif

#include "spi_stub.c"

struct esp_spi_resp *sif_get_spi_resp(void)
{
    return &spi_resp;
}

void sif_lock_bus(struct esp_pub *epub)
{
        EPUB_FUNC_CHECK(epub, _exit);

        spi_bus_lock(EPUB_TO_FUNC(epub)->master);
_exit:
	return;
}

void sif_unlock_bus(struct esp_pub *epub)
{
        EPUB_FUNC_CHECK(epub, _exit);

        spi_bus_unlock(EPUB_TO_FUNC(epub)->master);
_exit:
	return;
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
		esp_dbg(ESP_DBG_ERROR, "spierr %s: failed, error: %d\n",
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

int sif_spi_write_bytes(struct spi_device *spi, unsigned int addr, unsigned char *src,int count, int dummymode)
{
    int i;
    int pos,len;       
    unsigned char *tx_data = (unsigned char*)src;
    int err_ret = 0;     
    u32 timeout = 200;
    spi_resp.data_resp_size_w = ((((((count>>5)+1) *75)) +44)>>3) +1;

    if (spi == NULL) {
    	ESSERT(0);
	err_ret = -EINVAL;
	goto goto_err;
    }

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
            tx_cmd[3]=( addr<<1|(count>>8 & 0x01) );
            tx_cmd[4]= count & 0xff;     //0x08;
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
    memset(tx_cmd+pos,0xff,spi_resp.data_resp_size_w);
    pos = pos+ spi_resp.data_resp_size_w ;


    if(pos <128)
    {
        len = 128-pos;
        memset(tx_cmd+pos,0xff,len);
    }
    else
    {
        if( pos%8 )
        {
            len = (8 - pos%8);
            memset(tx_cmd+pos,0xff,len);
        }
        else
            len = 0;
    }

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

    if(i>spi_resp.max_cmd_resp_size)
    {
        spi_resp.max_cmd_resp_size = i;
    }

    if(i>=CMD_RESP_SIZE)
    {
        esp_dbg(ESP_DBG_ERROR, "byte write cmd resp 0x00 no recv\n");
        err_ret = -3;
        goto goto_err;
    }

    pos = pos+CMD_RESP_SIZE+count+1;
    for(i=0;i<spi_resp.data_resp_size_w;i++)
    {
        if(tx_cmd[pos+i] == 0xE5)
        {
            //esp_dbg(ESP_DBG_ERROR, "0xE5 pos:%d",i);
            for(i++;i<spi_resp.data_resp_size_w ;i++)
            {
                if( tx_cmd[pos+i] == 0xFF)
                {
       //     esp_dbg(ESP_DBG_ERROR, "find ff pos = %d,i = %d\n",pos+i,i);

                    if(i>spi_resp.max_dataW_resp_size)
                    {
                        spi_resp.max_dataW_resp_size = i;
                       // printk(KERN_ERR "new data write MAX 0xFF pos:%d",spi_resp.max_dataW_resp_size);
                    }

                    //esp_dbg(ESP_DBG_ERROR, "0xFF pos:%d",i);
                    break;
                }
            }
            break;      
        }
    }
    if(i>=spi_resp.data_resp_size_w)
    {
        if(dummymode == 0)
            esp_dbg(ESP_DBG, "normal byte write data no-busy wait byte 0xff no recv at the first time\n");

        timeout = 200;
        do {
            timeout --;
            //memset(check_buf,0x00,256);

            sif_spi_read_raw(spi, check_buf, 256);
        } while( memcmp(check_buf,ff_buf,256) != 0 && timeout >0 ) ;

        if(timeout == 0)
        {
            esp_dbg(ESP_DBG_ERROR, "spierr byte write data no-busy wait byte 0xff no recv \n"); 
            err_ret = -4;
            goto goto_err;
        }
    }

goto_err:
    return err_ret;
}

int sif_spi_write_blocks(struct spi_device *spi, unsigned int addr,unsigned char *src, int count)
{
    int err_ret = 0;
    int i,j;
    int n;
    int pos,len; 
    unsigned char *tx_data = (unsigned char*)src;
    int find_w_rsp = 0;
    int timeout = 200;

    if (spi == NULL) {
    	ESSERT(0);
	err_ret = -EINVAL;
	goto goto_err;
    }

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
            tx_cmd[3]=( addr<<1|(count>>8 & 0x01) );
            tx_cmd[4]= count & 0xff ;     
        }
    }
    tx_cmd[5]=0x01;

    pos = 5+1;
    //Add cmd respon
    memset(tx_cmd+pos,0xff,CMD_RESP_SIZE);
    pos =pos+ CMD_RESP_SIZE;
    if(count < 3)
    {
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
                memset(tx_cmd+pos , 0xff , spi_resp.block_w_data_resp_size_final);
                pos = pos+ spi_resp.block_w_data_resp_size_final;
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

        if(i>spi_resp.max_cmd_resp_size)
        {
            spi_resp.max_cmd_resp_size = i;
        }

        if(i>=CMD_RESP_SIZE)
        {
            esp_dbg(ESP_DBG_ERROR, "spierr 1st block write cmd resp 0x00 no recv, %d\n", count);
            err_ret = -3;
            goto goto_err;
        }

        pos = pos+CMD_RESP_SIZE;

        for(j=0;j<count;j++)
        {
            find_w_rsp = 0;
            //Judge block data resp
            pos = pos+1;                   
            pos = pos+SPI_BLOCK_SIZE;

            if( j==(count-1) )
                n = spi_resp.block_w_data_resp_size_final;
            else
                n= BLOCK_W_DATA_RESP_SIZE_EACH;

            for(i =0 ;i<4;i++)
            {
                if((tx_cmd[pos+i] & 0x0F) == 0x05)
                {
                    find_w_rsp = 1;
                    break;
                }
            }

            if(find_w_rsp == 1)
            {
                if(memcmp(tx_cmd+pos+n-4,ff_buf,4) != 0)
                {

                    esp_dbg(ESP_DBG, "block write  sleep 1\n");
                    if(j == count-1)
                    {
                        timeout = 200;
                        do {
                            timeout --;
                            //memset(check_buf,0x00,256);

                            sif_spi_read_raw(spi, check_buf, 256);
                        } while( memcmp(check_buf,ff_buf,256) != 0 && timeout >0 ) ;

                        if(timeout == 0)
                        {
                            err_ret = -7;
                            esp_dbg(ESP_DBG_ERROR, "spierr block write data no-busy wait byte 0xff no recv \n"); 
                            goto goto_err;
                        }
                    }
                    else
                    {
                        err_ret = -5;
                        esp_dbg(ESP_DBG_ERROR, "spierr %s block%d write data not-busy wait error, %d\n", __func__, j+1, count);
                        goto goto_err;
                    }
                }
            }
            else
            {
                err_ret = -6;
                esp_dbg(ESP_DBG_ERROR, "spierr %s block%d write data no data res error, %d\n", __func__, j+1, count);
                goto goto_err;
            }

            pos = pos+n;     
        }
    }
    else
    {
        for(j=0;j<2;j++)
        {
            //Add token   
            tx_cmd[pos]=0xFC;
            pos = pos+1;

            //Add data    
            memcpy(tx_cmd+pos,tx_data+j*SPI_BLOCK_SIZE, SPI_BLOCK_SIZE);
            pos = pos+SPI_BLOCK_SIZE;

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

        if(i>spi_resp.max_cmd_resp_size)
        {
            spi_resp.max_cmd_resp_size = i;
        }

        if(i>=CMD_RESP_SIZE)
        {
            esp_dbg(ESP_DBG_ERROR, "spierr 1st block write cmd resp 0x00 no recv, %d\n", count);
            err_ret = -3;
            goto goto_err;
        }

        pos = pos+CMD_RESP_SIZE;

        for(j=0;j<2;j++)
        {
            find_w_rsp = 0;
            //Judge block data resp
            pos = pos+1;                   
            pos = pos+SPI_BLOCK_SIZE;

            n = BLOCK_W_DATA_RESP_SIZE_EACH;

            for(i =0 ;i<4;i++)
            {
                if((tx_cmd[pos+i] & 0x0F) == 0x05)
                {
                    find_w_rsp = 1;
                    break;
                }
            }

            if(find_w_rsp == 1)
            {
                if(memcmp(tx_cmd+pos+n-4,ff_buf,4) != 0 )
                {
                    esp_dbg(ESP_DBG, "block write data during sleep \n"); 

                    if(j == 1)
                    {
                        timeout = 200;
                        do {
                            timeout --;

                            sif_spi_read_raw(spi, check_buf, 256);
                        } while( memcmp(check_buf,ff_buf,256) != 0 && timeout >0 ) ;

                        if(timeout == 0)
                        {
                            err_ret = -7;
                            esp_dbg(ESP_DBG_ERROR, "spierr block write data no-busy wait byte 0xff no recv \n"); 
                            goto goto_err;
                        }
                    }
                    else
                    {
                        err_ret = -5;
                        esp_dbg(ESP_DBG_ERROR, "spierr %s block%d write data not-busy wait error, %d\n", __func__, j+1, count);
                        goto goto_err;
                    }
                }
            }
            else
            {
                err_ret = -6;
                esp_dbg(ESP_DBG_ERROR, "spierr %s block%d write data no data res error, %d\n", __func__, j+1, count);
                goto goto_err;
            }

            pos = pos+n;   
        }
        

        pos = 0; 
        for(j=2;j<count;j++)
        {
            //Add token   
            tx_cmd[pos]=0xFC;
            pos = 1+pos ;
            //Add data    
            memcpy(tx_cmd+pos,tx_data+j*SPI_BLOCK_SIZE, SPI_BLOCK_SIZE);
            pos = pos+SPI_BLOCK_SIZE;
            //Add data respon
            if(j == count-1)
            {
                memset(tx_cmd+pos , 0xff ,spi_resp.block_w_data_resp_size_final);
                pos = pos+spi_resp.block_w_data_resp_size_final;
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
        
        pos = 0;
        for(j=2;j<count;j++)
        {
            find_w_rsp = 0;
            //Judge block data resp
            pos = pos+1;                   
            pos = pos+SPI_BLOCK_SIZE;

            if( j==(count-1) )
                n = spi_resp.block_w_data_resp_size_final;
            else
                n= BLOCK_W_DATA_RESP_SIZE_EACH;

            for(i =0 ;i<4;i++)
            {
                if((tx_cmd[pos+i] & 0x0F) == 0x05)
                {
                    find_w_rsp = 1;
                    break;
                }
            }

            if(find_w_rsp == 1)
            {
                if(memcmp(tx_cmd+pos+n-4,ff_buf,4) != 0)
                {
                    err_ret = -5;
                    esp_dbg(ESP_DBG_ERROR, "spierr %s block%d write data not-busy wait error, %d\n", __func__, j+1, count);
                    goto goto_err;
                }
            }
            else
            {
                err_ret = -6;
                esp_dbg(ESP_DBG_ERROR, "spierr %s block%d write data no data res error, %d\n", __func__, j+1, count);
                goto goto_err;
            }

            pos = pos+n;   
        }
    }

goto_err:

    return err_ret;
}

int sif_spi_write_mix_nosync(struct spi_device *spi, unsigned int addr, unsigned char *buf, int len, int dummymode)
{
	int blk_cnt;
	int remain_len;
	int err;
	do {
		blk_cnt = len/SPI_BLOCK_SIZE;
		remain_len = len%SPI_BLOCK_SIZE;

		if (blk_cnt > 0) {
			err  = sif_spi_write_blocks(spi, addr, buf, blk_cnt);
			if (err) 
				return err;
		}

		if (remain_len > 0) {
			err = sif_spi_write_bytes(spi, addr, (buf + (blk_cnt*SPI_BLOCK_SIZE)), remain_len, dummymode);
			if (err)
				return err;
		}
	} while(0);
	return 0;

}

int sif_spi_write_mix_sync(struct spi_device *spi, unsigned int addr, unsigned char *buf, int len, int dummymode) 
{
	int err;

	spi_bus_lock(spi->master);
	err = sif_spi_write_mix_nosync(spi, addr, buf, len, dummymode);
	spi_bus_unlock(spi->master);

	return err;
}

int sif_spi_epub_write_mix_nosync(struct esp_pub *epub, unsigned int addr, unsigned char *buf,int len, int dummymode)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	
	if (epub == NULL) {
		ESSERT(0);
		return -EINVAL;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return -EINVAL;
	}

	return sif_spi_write_mix_nosync(spi, addr, buf, len, dummymode);
}

int sif_spi_epub_write_mix_sync(struct esp_pub *epub, unsigned int addr, unsigned char *buf,int len, int dummymode)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
		
	if (epub == NULL) {
		ESSERT(0);
		return -EINVAL;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return -EINVAL;
	}

	return sif_spi_write_mix_sync(spi, addr, buf, len, dummymode);
}

int sif_spi_read_bytes(struct spi_device *spi, unsigned int addr,unsigned char *dst, int count, int dummymode)
{      
    int pos,total_num,len;
    int i;
    unsigned char *rx_data = (unsigned char *)dst;
    int err_ret = 0;
    unsigned short crc = 0;
    char test_crc[2];
    int unexp_byte = 0;

    u32 timeout = 200;
    int find_start_token = 0;
    spi_resp.data_resp_size_r = ((((((count>>2)+1) *25)>>5)*21+16)>>3) +1  ; 

    if (spi == NULL) {
    	ESSERT(0);
	err_ret = -EINVAL;
	goto goto_err;
    }

    rx_cmd[0]=0x75;

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
            rx_cmd[3]=( addr<<1|(count>>8 & 0x01) );
            rx_cmd[4]= count & 0xff;     //0x08;
        }
    }

    rx_cmd[5]=0x01;

    total_num = CMD_RESP_SIZE+spi_resp.data_resp_size_r+count+2;
    memset(rx_cmd+6 , 0xFF ,total_num);

    if(6+total_num <128)
    {
        len =128 -6 -total_num;
        memset(rx_cmd+6+total_num,0xff,len);
    }
    else
    {

        if( (6+total_num)%8 )
        {
            len = (8 - (6+total_num)%8);
            memset(rx_cmd+6+total_num,0xff,len);
        }
        else
            len = 0;

    }  

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

    if(i>spi_resp.max_cmd_resp_size)
    {
        spi_resp.max_cmd_resp_size = i;
        //printk(KERN_ERR "new cmd write MAX 0xFF pos:%d",spi_resp.max_cmd_resp_size);
    }


    if(i>=CMD_RESP_SIZE)
    {
        esp_dbg(ESP_DBG_ERROR, "spierr byte read cmd resp 0x00 no recv\n");
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

    for(i=0;i<spi_resp.data_resp_size_r;i++)
    {
        if(rx_cmd[pos+i]==0xFE)
        {

            find_start_token = 1;   
            if(i>spi_resp.max_dataR_resp_size)
            {
                spi_resp.max_dataR_resp_size = i;
                //printk(KERN_ERR "new data read MAX 0xFE pos:%d",spi_resp.max_dataR_resp_size);
            }
            break;
        }
        else if(rx_cmd[pos+i] != 0xff)
        {
            unexp_byte ++;
            if(unexp_byte == 1)
                esp_dbg(ESP_DBG, " 1 normal byte read not 0xFF or 0xFE  at the first time,count = %d,addr =%x \n",count,addr);
        }
    }

    if(find_start_token == 0) 
    { 
        if(dummymode == 0)  
            esp_dbg(ESP_DBG, " normal byte read start token 0xFE  not recv at the first time,count = %d,addr =%x \n",count,addr);

        pos = pos +spi_resp.data_resp_size_r;

        for(i=0;i< 6+total_num+len-pos;i++)
        {
            if(rx_cmd[pos+i]==0xFE)
            {
                sif_spi_read_raw(spi,rx_cmd,((count+4)>256)?(count+4):256);
               // sif_spi_read_raw(spi, check_buf, 256);
                find_start_token = 1;
                err_ret = -4;
                goto goto_err;
            }
            else if(rx_cmd[pos+i] !=0xff)
            {
                unexp_byte ++;
                if(unexp_byte == 1)
                    esp_dbg(ESP_DBG, "2 normal byte read not 0xFF or 0xFE  at the first time,count = %d,addr =%x \n",count,addr);
            }

        }

        timeout = 200;
        do {
            timeout --;

            sif_spi_read_raw(spi, check_buf, 256);
            for(i=0;i<256;i++)
            {
                if(check_buf[i] ==0xFE)
                {
                    find_start_token = 1;
                    sif_spi_read_raw(spi,rx_cmd,((count+4)>256)?(count+4):256);

                    break;
                }
            }
        } while( find_start_token != 1 && timeout >0 ) ;

        if(timeout == 0)
        {
            esp_dbg(ESP_DBG_ERROR, "spierr byte read start token 0xFE no recv ,count = %d,addr =%x \n",count,addr);
        }
        err_ret = -5;

        goto goto_err;
    }

    //esp_dbg(ESP_DBG_ERROR, "0xFE pos:%d",pos+i);
    pos = pos+i+1;
    memcpy(rx_data,rx_cmd+pos,count);

    crc = crc_cal_by_byte(rx_data,count);

    test_crc[0] = crc & 0xff;
    test_crc[1] = (crc >>8) &0xff ;

    if(test_crc[1] != rx_cmd[pos+count] || test_crc[0] != rx_cmd[pos+count+1] )
    {
        esp_dbg(ESP_DBG, "crc test_crc0  %x\n",test_crc[0]);
        esp_dbg(ESP_DBG, "crc test_crc1  %x\n",test_crc[1]);
        esp_dbg(ESP_DBG, "crc rx_data-2  %x\n",rx_cmd[pos+count]);
        esp_dbg(ESP_DBG, "crc rx_data-1  %x\n",rx_cmd[pos+count+1] );

        esp_dbg(ESP_DBG, "crc err\n");

        esp_dbg(ESP_DBG_ERROR, "spierr crc count = %d,addr =%x \n",count,addr);

        err_ret = -6;
        goto goto_err;

    }

goto_err:

    return err_ret;
}

int sif_spi_read_blocks(struct spi_device *spi, unsigned int addr, unsigned char *dst, int count)
{
    int err_ret = 0;
    int pos,len;
    int i,j;
    unsigned char *rx_data = (unsigned char *)dst;
    int total_num;
    u32 timeout = 200;
    int find_start_token = 0;
    unsigned short crc = 0;
    char test_crc[2];
   
    if (spi == NULL) {
    	ESSERT(0);
	err_ret = -EINVAL;
	goto goto_err;
    }

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
            rx_cmd[3]=( addr<<1|(count>>8 & 0x01) );
            rx_cmd[4]= count & 0xff;     
        }
    }
    rx_cmd[5]=0x01;
    total_num = CMD_RESP_SIZE+spi_resp.block_r_data_resp_size_final+SPI_BLOCK_SIZE+ 2 + (count-1)*(BLOCK_R_DATA_RESP_SIZE_EACH+SPI_BLOCK_SIZE+2);
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

    if(i>spi_resp.max_cmd_resp_size)
    {
        spi_resp.max_cmd_resp_size = i;
        //printk(KERN_ERR "new cmd write MAX 0xFF pos:%d",spi_resp.max_cmd_resp_size);
    }

    if(i>=CMD_RESP_SIZE)
    {
        esp_dbg(ESP_DBG_ERROR, "spierr block read cmd resp 0x00 no recv\n");

        err_ret = -3;
        goto goto_err;
    }

    pos = pos+i+2;

    for(i=0;i<spi_resp.block_r_data_resp_size_final;i++)
    {
        if(rx_cmd[pos+i]==0xFE)
        {
            //esp_dbg(ESP_DBG_ERROR, "0xFE pos:%d",i);
            find_start_token = 1;
            if(i>spi_resp.max_block_dataR_resp_size)
            {
                spi_resp.max_block_dataR_resp_size = i;
                //printk(KERN_ERR "new block data read MAX 0xFE pos:%d\n",spi_resp.max_block_dataR_resp_size);   
            }

            break;
        }
    }

    if( find_start_token == 0) 
    {       
        esp_dbg(ESP_DBG, "1st block read data resp 0xFE no recv,count = %d\n",count);
        pos = pos +spi_resp.block_r_data_resp_size_final;
        for(i=0;i< 6+total_num+len-pos;i++)
        {
            if(rx_cmd[pos+i]==0xFE)
            {
                sif_spi_read_raw(spi, check_buf, total_num+len);
                find_start_token = 1;
                err_ret = -4;
                goto goto_err;
            }
        }

        timeout = 200;
        do {
            esp_dbg(ESP_DBG_ERROR, "block read  sleep ,count = %d\n",count);
            timeout --;
            sif_spi_read_raw(spi, check_buf, 256);
            for(i=0;i<256;i++)
            {
                if(check_buf[i] ==0xFE)
                {
                    find_start_token= 1;
                    sif_spi_read_raw(spi, rx_cmd, total_num+len);
                    break;
                }
            }
        } while( find_start_token != 1 && timeout >0 ) ;
        if(timeout == 0)
        {
            err_ret = -8;
            esp_dbg(ESP_DBG_ERROR, "spierr block read start token 0xFE no recv\n");
        }else
        {
            err_ret = -5;
        }
        goto goto_err;
    }

    pos = pos+i+1;

    memcpy(rx_data,rx_cmd+pos,SPI_BLOCK_SIZE);

    crc = crc_cal_by_byte(rx_data,512);

    test_crc[0] = crc & 0xff;
    test_crc[1] = (crc >>8) &0xff ;

    if(test_crc[1] != rx_cmd[pos+SPI_BLOCK_SIZE] || test_crc[0] != rx_cmd[pos+SPI_BLOCK_SIZE+1] )
    {   
        esp_dbg(ESP_DBG, "crc test_crc0  %x\n",test_crc[0]);
        esp_dbg(ESP_DBG, "crc test_crc1  %x\n",test_crc[1]);
        esp_dbg(ESP_DBG, "crc rx_data-2  %x\n",rx_cmd[pos+SPI_BLOCK_SIZE]);
        esp_dbg(ESP_DBG, "crc rx_data-1  %x\n",rx_cmd[pos+SPI_BLOCK_SIZE+1] );

        esp_dbg(ESP_DBG_ERROR, "spierr crc err block = 1,count = %d\n",count);

        err_ret = -6;
        goto goto_err;

    }

    pos = pos +SPI_BLOCK_SIZE + 2;

    for(j=1;j<count;j++)
    {

        for(i=0;i<BLOCK_R_DATA_RESP_SIZE_EACH;i++)
        {
            if(rx_cmd[pos+i]==0xFE)
            {
                //esp_dbg(ESP_DBG_ERROR, "0xFE pos:%d",i);
                if(i>spi_resp.max_block_dataR_resp_size)
                {
                    spi_resp.max_block_dataR_resp_size = i;
                    //printk(KERN_ERR "new block data read MAX 0xFE pos:%d",spi_resp.max_block_dataR_resp_size);   
                }

                break;
            }
        }
        if(i>=BLOCK_R_DATA_RESP_SIZE_EACH)
        {
            esp_dbg(ESP_DBG_ERROR, "spierr block%d read data token 0xFE no recv,total:%d\n",j+1,count);
            err_ret = -7;
            goto goto_err;
        }

        pos = pos+i+1;

        memcpy(rx_data+j*SPI_BLOCK_SIZE,rx_cmd+pos,SPI_BLOCK_SIZE);
  
        crc = crc_cal_by_byte(rx_data+j*SPI_BLOCK_SIZE ,512);

        test_crc[0] = crc & 0xff;
        test_crc[1] = (crc >>8) &0xff ;

        if(test_crc[1] != rx_cmd[pos+SPI_BLOCK_SIZE] || test_crc[0] != rx_cmd[pos+SPI_BLOCK_SIZE+1] )
        {
            esp_dbg(ESP_DBG, "crc test_crc0  %x\n",test_crc[0]);
            esp_dbg(ESP_DBG, "crc test_crc1  %x\n",test_crc[1]);
            esp_dbg(ESP_DBG, "crc rx_data-2  %x\n",rx_cmd[pos+SPI_BLOCK_SIZE]);
            esp_dbg(ESP_DBG, "crc rx_data-1  %x\n",rx_cmd[pos+SPI_BLOCK_SIZE+1] );

            esp_dbg(ESP_DBG_ERROR, "spierr crc err,count = %d,block =%d\n",count,j+1);
 
            err_ret = -6;
            goto goto_err;
        }

        pos = pos +SPI_BLOCK_SIZE + 2;
    }

goto_err:       

    return err_ret;
}

int sif_spi_read_mix_nosync(struct spi_device *spi, unsigned int addr, unsigned char *buf, int len, int dummymode)
{
	int blk_cnt;
	int remain_len;
	int err = 0;
	int retry = 20;

	do{
		blk_cnt = len/SPI_BLOCK_SIZE;
		remain_len = len%SPI_BLOCK_SIZE;

		if (blk_cnt > 0) {

			retry = 20;
			do {
				if(retry < 20)
					mdelay(10);
				retry--;
				check_buf[0] = 1<<2;
				err  = sif_spi_read_blocks(spi, addr, buf, blk_cnt);
				if(err == 0)
				{
					sif_spi_write_bytes(spi,SLC_HOST_CONF_W4 + 2,check_buf,1,0);
				} else if(err == -4 ||err == -5 ||err == -6||err == -7 ||err == -8)
				{
					struct esp_spi_ctrl *sctrl = spi_get_drvdata(spi);
					if(sctrl != NULL) {
						sif_ack_target_read_err(sctrl->epub);
					}
				} else if(err == -3)
				{
					continue;
				} else
				{
					break;
				}

			}while(retry > 0 && err != 0);
			if(err != 0 && retry == 0)
				esp_dbg(ESP_DBG_ERROR, "spierr 20 times retry block read fail\n");

			if(err)
				return err;
		}

		if (remain_len > 0) {
			if(dummymode == 0 )
			{
				retry = 20;
				do{
					if(retry <20)
						mdelay(10);
					retry--;
					err = sif_spi_read_bytes(spi, addr, (buf + (blk_cnt*SPI_BLOCK_SIZE)), remain_len, dummymode);
				}while(retry >0 && err != 0);

				if(err != 0 &&  retry == 0)
					esp_dbg(ESP_DBG_ERROR, "spierr 20 times retry byte read fail\n");
			}
			else
			{
				err = sif_spi_read_bytes(spi, addr, (buf + (blk_cnt*SPI_BLOCK_SIZE)), remain_len, dummymode);
			}
			if (err)
				return err;
		}
	} while(0);
	return 0;
}

int sif_spi_read_mix_sync(struct spi_device *spi, unsigned int addr, unsigned char *buf, int len, int dummymode)
{
	int err;
	
	spi_bus_lock(spi->master);
	err = sif_spi_read_mix_nosync(spi, addr, buf, len, dummymode);
	spi_bus_unlock(spi->master);

	return err;
}

int sif_spi_epub_read_mix_nosync(struct esp_pub *epub, unsigned int addr, unsigned char *buf,int len, int dummymode)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
		
	if (epub == NULL) {
		ESSERT(0);
		return -EINVAL;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return -EINVAL;
	}

	return sif_spi_read_mix_nosync(spi, addr, buf, len, dummymode);
}

int sif_spi_epub_read_mix_sync(struct esp_pub *epub, unsigned int addr, unsigned char *buf,int len, int dummymode)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;

	if (epub == NULL) {
		ESSERT(0);
		return -EINVAL;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return -EINVAL;
	}

	return sif_spi_read_mix_sync(spi, addr, buf, len, dummymode);
}

int sif_spi_read_sync(struct esp_pub *epub, unsigned char *buf, int len, int dummymode)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	u32 read_len;

	if (epub == NULL || buf == NULL) {
		ESSERT(0);
		return -EINVAL;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return -EINVAL;
	}

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
	
	return sif_spi_read_mix_sync(spi, sctrl->slc_window_end_addr - 2 - (len), buf, read_len, dummymode);
}

int sif_spi_write_sync(struct esp_pub *epub, unsigned char *buf, int len, int dummymode)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
        u32 write_len;

	if (epub == NULL || buf == NULL) {
		ESSERT(0);
		return -EINVAL;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return -EINVAL;
	}

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
	return sif_spi_write_mix_sync(spi, sctrl->slc_window_end_addr - (len), buf, write_len, dummymode);
}

int sif_spi_read_nosync(struct esp_pub *epub, unsigned char *buf, int len, int dummymode, bool noround)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
	u32 read_len;

	if (epub == NULL || buf == NULL) {
		ESSERT(0);
		return -EINVAL;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return -EINVAL;
	}

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
	
	return sif_spi_read_mix_nosync(spi, sctrl->slc_window_end_addr - 2 - (len), buf, read_len, dummymode);
}

int sif_spi_write_nosync(struct esp_pub *epub, unsigned char *buf, int len, int dummymode)
{
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;
        u32 write_len;

	if (epub == NULL || buf == NULL) {
		ESSERT(0);
		return -EINVAL;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return -EINVAL;
	}

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
	return sif_spi_write_mix_nosync(spi, sctrl->slc_window_end_addr - (len), buf, write_len, dummymode);
}

int sif_spi_protocol_init(struct spi_device *spi)
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
			int fail_count = 0;
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
                                          //  esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
             //   ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                                mdelay(100);
				if(fail_count++ > 10)
					return -ETIMEDOUT;
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
                                     //  esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
              //  ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
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
                                     //  esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
             //   ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
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
                                    //   esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
             //   ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
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
                                    //   esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
              //  ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
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
                                    //   esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
             //   ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                                      
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
                                     //  esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
              //  ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
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
                                    //   esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
            //    ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
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
                                    //   esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
              //  ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);
                        
                        //spi_err("CMD52 Write Reg addr 0x111 \n");  
                        tx_buf1[0]=0x74;
                        tx_buf1[1]=0x80;
                        tx_buf1[2]=0x02;               
                        tx_buf1[3]=0x22;
                        tx_buf1[4]=(unsigned char)(SPI_BLOCK_SIZE>>8);                      //0x00;
                        tx_buf1[5]=0x01;
                        sif_spi_write_raw(spi, tx_buf1, 6);
                        sif_spi_write_async_read(spi,dummy_tx_buf, rx_buf1,10);
                                    //   esp_dbg(ESP_DBG_ERROR, "rx:[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x],[0x%02x]\n", rx_buf1[0],rx_buf1[1]
              //  ,rx_buf1[2],rx_buf1[3],rx_buf1[4],rx_buf1[5],rx_buf1[6],rx_buf1[7],rx_buf1[8],rx_buf1[9]);

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
	return 0;
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

	if (epub == NULL) {
		ESSERT(0);
		return;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return;
	}

	mdelay(100);

	sif_platform_irq_init();
	err = sif_setup_irq_thread(spi);
        if (err) {
                esp_dbg(ESP_DBG_ERROR, "%s setup sif irq failed\n", __func__);
		return;
	}

/******************compat with other device in some shared irq system ********************/

#ifdef  REQUEST_IRQ_SHARED
#if   defined(REQUEST_IRQ_RISING)
        err = request_irq(sif_platform_get_irq_no(), sif_irq_handler, IRQF_TRIGGER_RISING | IRQF_SHARED, "esp_spi_irq", spi);
#elif defined(REQUEST_IRQ_FALLING)
        err = request_irq(sif_platform_get_irq_no(), sif_irq_handler, IRQF_TRIGGER_FALLING | IRQF_SHARED, "esp_spi_irq", spi);
#elif defined(REQUEST_IRQ_LOWLEVEL)
        err = request_irq(sif_platform_get_irq_no(), sif_irq_handler, IRQF_TRIGGER_LOW | IRQF_SHARED, "esp_spi_irq", spi);
#elif defined(REQUEST_IRQ_HIGHLEVEL)
        err = request_irq(sif_platform_get_irq_no(), sif_irq_handler, IRQF_TRIGGER_HIGH | IRQF_SHARED, "esp_spi_irq", spi);
#else   /* default */
        err = request_irq(sif_platform_get_irq_no(), sif_irq_handler, IRQF_TRIGGER_LOW | IRQF_SHARED, "esp_spi_irq", spi);
#endif /* TRIGGER MODE */
#else
        err = request_irq(sif_platform_get_irq_no(), sif_irq_handler, IRQF_TRIGGER_LOW, "esp_spi_irq", spi);
#endif /* ESP_IRQ_SHARED */

        if (err) {
                esp_dbg(ESP_DBG_ERROR, "sif %s failed\n", __func__);
		return ;
	}
#ifdef IRQ_WAKE_HOST
	enable_irq_wake(sif_platform_get_irq_no());
#endif
        atomic_set(&sctrl->irq_installed, 1);
}

void sif_disable_irq(struct esp_pub *epub) 
{
	int i = 0;
	struct esp_spi_ctrl *sctrl = NULL;
        struct spi_device *spi = NULL;

	if (epub == NULL) {
		ESSERT(0);
		return;
	}
        sctrl = (struct esp_spi_ctrl *)epub->sif;
        spi = sctrl->spi;
	if (spi == NULL) {
		ESSERT(0);
		return;
	}

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
#ifndef ESP_PREALLOC
	int retry = 10;
#endif
	/**** alloc buffer for spi io */
	if (sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT) {
#ifdef ESP_PREALLOC
		if ((buf_addr = esp_get_lspi_buf()) == NULL)
			goto _err_buf_addr;
#else
		while ((buf_addr = (unsigned char *)kmalloc (MAX_BUF_SIZE, GFP_KERNEL)) == NULL) {
				if (--retry < 0)
					goto _err_buf_addr;
		}
#endif
        	if ((check_buf = (unsigned char *)kmalloc (256, GFP_KERNEL)) == NULL)
					goto _err_check_buf;

        	if ((ff_buf = (unsigned char *)kmalloc (256, GFP_KERNEL)) == NULL)
					goto _err_ff_buf;
        
        	memset(ff_buf,0xff,256);

		tx_cmd = buf_addr;
        	rx_cmd = buf_addr;
    	}
	spi_resp.max_dataW_resp_size = 0;
	spi_resp.max_dataR_resp_size = 0;
	spi_resp.max_block_dataW_resp_size = 0;
	spi_resp.max_block_dataR_resp_size = 0;
	spi_resp.max_cmd_resp_size = 0;
	if( sif_get_ate_config() != 5)
	{
        	spi_resp.data_resp_size_w = DATA_RESP_SIZE_W;
        	spi_resp.data_resp_size_r = DATA_RESP_SIZE_R;
        	spi_resp.block_w_data_resp_size_final = BLOCK_W_DATA_RESP_SIZE_FINAL;
        	spi_resp.block_r_data_resp_size_final = BLOCK_R_DATA_RESP_SIZE_1ST;
	} else {
        	spi_resp.data_resp_size_w = 1000;
        	spi_resp.data_resp_size_r = 1000;
        	spi_resp.block_w_data_resp_size_final = 1000;
        	spi_resp.block_r_data_resp_size_final = 1000;
	}

	return 0;

_err_ff_buf:
	if (check_buf) {
		kfree(check_buf);
		check_buf = NULL;
	}
_err_check_buf:
	if (buf_addr) {
#ifdef ESP_PREALLOC
		esp_put_lspi_buf(&buf_addr);
#else
		kfree(buf_addr);
#endif
		buf_addr = NULL;
	}
_err_buf_addr:
	return -ENOMEM;
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
                if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT)
			goto _err_spi;
		else
			goto _err_second_init;
	}
	esp_dbg(ESP_DBG_ERROR, "%s init_protocol\n", __func__);
	err = sif_spi_protocol_init(spi);
	if(err){
                if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT)
			goto _err_spi;
		else
			goto _err_second_init;
	}

	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		sctrl = kzalloc(sizeof(struct esp_spi_ctrl), GFP_KERNEL);

		if (sctrl == NULL) {
                	err = -ENOMEM;
			goto _err_spi;
		}

		/* temp buffer reserved for un-dma-able request */
		sctrl->dma_buffer = kzalloc(ESP_DMA_IBUFSZ, GFP_KERNEL);

		if (sctrl->dma_buffer == NULL) {
                	err = -ENOMEM;
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
		if (sif_get_ate_config() == 0) {
			err = ext_gpio_init(epub);
			if (err) {
                		esp_dbg(ESP_DBG_ERROR, "ext_irq_work_init failed %d\n", err);
				goto _err_epub;
			}
		}
#endif
	} else {
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
                	goto _err_ext_gpio;
		else
			goto _err_second_init;
        }
        check_target_id(epub);

        err = esp_pub_init_all(epub);

        if (err) {
                esp_dbg(ESP_DBG_ERROR, "esp_init_all failed: %d\n", err);
                if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
			err = 0;
			goto _err_first_init;
		}
                if(sif_sdio_state == ESP_SDIO_STATE_SECOND_INIT)
			goto _err_second_init;
        }

        esp_dbg(ESP_DBG_TRACE, " %s return  %d\n", __func__, err);
	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		esp_dbg(ESP_DBG_ERROR, "first normal exit\n");
		sif_sdio_state = ESP_SDIO_STATE_FIRST_NORMAL_EXIT;
		up(&esp_powerup_sem);
	}

        return err;
_err_ext_gpio:
#ifdef USE_EXT_GPIO	
	if (sif_get_ate_config() == 0)
		ext_gpio_deinit();
_err_epub:
#endif
        esp_pub_dealloc_mac80211(epub);
_err_dma:
        kfree(sctrl->dma_buffer);
_err_last:
        kfree(sctrl);
_err_spi:
	if (buf_addr) {
#ifdef ESP_PREALLOC
		esp_put_lspi_buf(&buf_addr);
#else
		kfree(buf_addr);
#endif
		buf_addr = NULL;
		tx_cmd = NULL;
		rx_cmd = NULL;
	}
	if (check_buf) {
        	kfree(check_buf);
        	check_buf = NULL;
	}
	if (ff_buf) {
        	kfree(ff_buf);
        	ff_buf = NULL;
	}
	
_err_first_init:
	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		esp_dbg(ESP_DBG_ERROR, "first error exit\n");
		sif_sdio_state = ESP_SDIO_STATE_FIRST_ERROR_EXIT;
		up(&esp_powerup_sem);
	}
        return err;
_err_second_init:
	sif_sdio_state = ESP_SDIO_STATE_SECOND_ERROR_EXIT;
	esp_spi_remove(spi);
	return err;
}

static int esp_spi_remove(struct spi_device *spi) 
{
        struct esp_spi_ctrl *sctrl = NULL;

	esp_dbg(ESP_SHOW, "%s \n", __func__);

        sctrl = spi_get_drvdata(spi);

        if (sctrl == NULL) {
                esp_dbg(ESP_DBG_ERROR, "%s no sctrl\n", __func__);
                return -EINVAL;
        }

        do {
                if (sctrl->epub == NULL) {
                        esp_dbg(ESP_DBG_ERROR, "%s epub null\n", __func__);
                        break;
                }
		sctrl->epub->sdio_state = sif_sdio_state;
		if(sif_sdio_state != ESP_SDIO_STATE_FIRST_NORMAL_EXIT){
                	if (sctrl->epub->sip) {
                        	sip_detach(sctrl->epub->sip);
                        	sctrl->epub->sip = NULL;
                        	esp_dbg(ESP_DBG_TRACE, "%s sip detached \n", __func__);
                	}
#ifdef USE_EXT_GPIO	
			if (sif_get_ate_config() == 0)
				ext_gpio_deinit();
#endif
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
#ifdef ESP_PREALLOC
				esp_put_lspi_buf(&buf_addr);
#else
				kfree(buf_addr);
#endif
				buf_addr = NULL;
				rx_cmd = NULL;
				tx_cmd = NULL;
			}
            
            		if (check_buf) {
                		kfree(check_buf);
                		check_buf = NULL;
            		}
            
            		if (ff_buf) {
                		kfree(ff_buf);
                		ff_buf = NULL;
            		}

		}

        } while (0);
        
	spi_set_drvdata(spi,NULL);
	
        esp_dbg(ESP_DBG_TRACE, "eagle spi remove complete\n");

	return 0;
}

static int esp_spi_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
        struct esp_spi_ctrl *sctrl = spi_get_drvdata(spi);
	struct esp_pub *epub = sctrl->epub;

        printk("%s", __func__);
#if 0
        sip_send_suspend_config(epub, 1);
#endif
        atomic_set(&epub->ps.state, ESP_PM_ON);	
	return 0;
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
		.pm = &esp_spi_pm_ops,
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

int esp_spi_init(void) 
{
#define ESP_WAIT_UP_TIME_MS 11000
        int err;
        int retry = 3;
        bool powerup = false;

        esp_dbg(ESP_DBG_TRACE, "%s \n", __func__);

#ifdef REGISTER_SPI_BOARD_INFO
	sif_platform_register_board_info();
#endif

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
                                 msecs_to_jiffies(ESP_WAIT_UP_TIME_MS)) == 0 && sif_get_ate_config() == 0) {
		if(sif_sdio_state == ESP_SDIO_STATE_FIRST_NORMAL_EXIT){
                	spi_unregister_driver(&esp_spi_driver);

			msleep(100);
                
			sif_sdio_state = ESP_SDIO_STATE_SECOND_INIT;
        	
			spi_register_driver(&esp_spi_driver);
		}
                
        }

        esp_register_early_suspend();
	esp_wake_unlock();
#ifdef REQUEST_RTC_IRQ
	request_rtc_irq();
#endif
        return err;

_fail:
        esp_wake_unlock();
        esp_wakelock_destroy();

        return err;
}

void esp_spi_exit(void) 
{
	esp_dbg(ESP_SHOW, "%s \n", __func__);

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
