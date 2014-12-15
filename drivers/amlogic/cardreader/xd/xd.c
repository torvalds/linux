/*****************************************************************
**                                                              **
**  Copyright (C) 2004 Amlogic,Inc.                             **
**  All rights reserved                                         **
**        Filename : sd.c /Project:AVOS  driver                 **
**        Revision : 1.0                                        **
**                                                              **
*****************************************************************/

#include <linux/slab.h>

#include <asm/drivers/cardreader/cardreader.h>
#include <asm/drivers/cardreader/card_io.h>
#include <asm/drivers/cardreader/xd.h>

#include "xd_sm.h"

static XD_Card_Info_t xd_info;
unsigned char xd_reset_flag_read, xd_reset_flag_write;

unsigned char xd_insert_detector(void)
{
	int ret = xd_check_insert();
	if(ret)
	{
        return CARD_INSERTED;
    }
    else
    {
        return CARD_REMOVED;
    }
}

unsigned char xd_open(void)
{
	int ret;
	
	xd_reset_flag_read = 0;
	xd_reset_flag_write = 0;
	ret = xd_card_init(&xd_info);
	if(ret)
	    ret = xd_card_init(&xd_info);
	if(ret)
		return CARD_UNIT_READY;
	else
		return CARD_UNIT_PROCESSED;
}

unsigned char xd_close(void)
{
	xd_exit();
	return CARD_UNIT_PROCESSED;
}

unsigned char xd_read_info(u32 *blk_length, u32 *capacity, u32 *raw_cid)
{
	if(xd_info.xd_inited_flag)
	{
		if(blk_length)
			*blk_length = 512;
		if(capacity)
			*capacity = xd_info.blk_nums;
		if(raw_cid)
			memcpy(raw_cid, &(xd_info.raw_cid), sizeof(xd_info.raw_cid));
		return 0;
	}
	else
		return 1;
}

int xd_ioctl(dev_t dev, int req, void *argp)
{
/*	unsigned32 ret=0;
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
                    ret = xd_read_data(r->start,r->count*512,(unsigned char *)databuf);
                    if (ret)
                    	ret = xd_read_data(r->start,r->count*512,(unsigned char *)databuf);
                    if (ret)
                    {
                    	xd_reset_flag_read = 1;
                    	ret = xd_read_data(r->start,r->count*512,(unsigned char *)databuf);
                    	xd_reset_flag_read = 0;
                    }
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
                    ret = xd_write_data(r->start,r->count*512,(unsigned char *)databuf);
                    if (ret)
                    	ret = xd_write_data(r->start,r->count*512,(unsigned char *)databuf);
                    if (ret)
                    {
                    	xd_reset_flag_write = 1;
                    	ret = xd_write_data(r->start,r->count*512,(unsigned char *)databuf);
                    	xd_reset_flag_write = 0;
                    }
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
            xd_read_info(NULL, &capacity);
            r->status = capacity;
            break;
        }
        case BLKIO_GET_TYPE_STR:
        {
        	*(char **)argp = "XD";
        	break;
    	}
        default:
        {
            errno = EBADRQC;
            ret=-1;
            break;
        }
    }*/
    return 0;//ret; 
}

void xd_init(void)
{
	xd_io_init();
	xd_prepare_init();
}

