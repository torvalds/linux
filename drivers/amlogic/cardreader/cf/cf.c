/*****************************************************************
**                                                              **
**  Copyright (C) 2004 Amlogic,Inc.                             **
**  All rights reserved                                         **
**        Filename : cf.c /Project:AVOS  driver                 **
**        Revision : 1.0                                        **
**                                                              **
*****************************************************************/
#include <linux/slab.h>

#include <asm/drivers/cardreader/cardreader.h>
#include <asm/drivers/cardreader/card_io.h>
#include <asm/drivers/cardreader/cf.h>

#include "cf_protocol.h"
#include "../ata/ata_protocol.h"

static CF_Card_Info_t cf_info;
extern unsigned cf_retry_init;

unsigned char cf_insert_detector(void)
{
	int ret = cf_check_insert();
	if(ret)
	{
        return CARD_INSERTED;
    }
    else
    {
        return CARD_REMOVED;
    }
}

unsigned char cf_open(void)
{
	int ret;

	ret = cf_card_init(&cf_info);
	if(ret)
	{
		cf_retry_init = 1;
		ret = cf_card_init(&cf_info);
		cf_retry_init = 0;
	}
	
	if(ret)
		return CARD_UNIT_READY;
	else
		return CARD_UNIT_PROCESSED;
}

unsigned char cf_close(void)
{
	cf_power_off();
	cf_exit();
	return CARD_UNIT_PROCESSED;
}

unsigned char cf_read_info(unsigned int *blk_length, unsigned int *capacity, u32 *raw_cid)
{
	if(cf_info.inited_flag)
	{
		if(blk_length)
			*blk_length = 512;
		if(capacity)
			*capacity = cf_info.blk_nums;
		if(raw_cid)
			memcpy(raw_cid, &(cf_info.serial_number), 16);
		return 0;
	}
	else
		return 1;
}

int cf_ioctl(dev_t dev, int req, void *argp)
{
	/*unsigned32 ret=0;
    int errno;
    blkdev_request1 *req1 =(blkdev_request1 *) argp;  
    blkdev_request *r =&(req1->req); 
    blkdev_sg_buffer* psgbuf ;
    void * databuf;
    avfs_status_code status;
    INT32U capacity = 0;

    switch (req)
    {
        case BLKIO_REQUEST:
        {
            psgbuf=&(r->bufs[0]);
            databuf=psgbuf->buffer;
            switch (r->req)
            {
                case BLKDEV_REQ_READ:
                case BLKDEV_REQ_READ_DEV:
                	card_get_dev();
                    ret = cf_read_data(r->start,r->count*512,(INT8U *)databuf);
                    card_put_dev();
                    if(!ret)
                        status=AVFS_SUCCESSFUL;
                    else
                        status=AVFS_IO_ERROR;                   
                    if(r->req_done)
                        r->req_done(r->done_arg, status, ret);       
                    break;
                case BLKDEV_REQ_WRITE:
                case BLKDEV_REQ_WRITE_DEV:
                	card_get_dev();
                    ret = cf_write_data(r->start,r->count*512,(INT8U *)databuf);
                    card_put_dev();
                    if(!ret)
                        status=AVFS_SUCCESSFUL;
                    else
                        status=AVFS_IO_ERROR;                       
                    if(r->req_done)
                        r->req_done(r->done_arg, status, ret);       
                    break;
                case BLKDEV_REQ_ASYREAD_DEV:
                case BLKDEV_REQ_ASYREAD_MEM:
                    break;
                default:
                    errno = EBADRQC;
                    ret=-1;
                    break;
            }
            break;
        }
        case BLKIO_GETSIZE:
        {
            cf_read_info(NULL, &capacity);
            r->status = capacity;
            break;
        }
        case BLKIO_GET_TYPE_STR:
        {
        	*(char **)argp = "CF";
        	break;
    	}
    	case BLKIO_GET_DEVSTAT:
    	{
    		blkdev_stat_t *info = (blkdev_stat_t *)argp;
    		cf_get_info(info);
    		break;
    	}    	
        default:
        {
            errno = EBADRQC;
            ret=-1;
            break;
        }
    }*/
    return 0; 
}

void cf_init(void)
{
	cf_io_init();
	cf_prepare_init();
}
