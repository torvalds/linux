/*
 * VDIN vframe support
 *
 * Author: Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/* Standard Linux headers */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>


/* Amlogic Headers */
#include <linux/amlogic/amports/vframe.h>

/* Local Headers */
#include "vdin_vf.h"

static bool vf_log_enable = true;
static bool vf_log_fe = true;
static bool vf_log_be = true;

module_param(vf_log_enable, bool, 0664);
MODULE_PARM_DESC(vf_log_enable, "enable/disable vframe manager log");

module_param(vf_log_fe, bool, 0664);
MODULE_PARM_DESC(vf_log_fe, "enable/disable vframe manager log frontend");

module_param(vf_log_be, bool, 0664);
MODULE_PARM_DESC(vf_log_be, "enable/disable vframe manager log backen");


#ifdef VF_LOG_EN
inline void vf_log_init(struct vf_pool *p)
{
	memset(&p->log, 0, sizeof(struct vf_log_s));
}
#else
inline void vf_log_init(struct vf_pool *p)
{
}
#endif

#ifdef VF_LOG_EN
static void vf_log(struct vf_pool *p, enum vf_operation_e operation, bool operation_done)
{
	unsigned int i = 0;
	struct vf_log_s *log = &p->log;

	if (!vf_log_enable)
		return;

	if (!vf_log_fe)
		if (operation == VF_OPERATION_FPEEK ||
		    operation == VF_OPERATION_FGET ||
		    operation == VF_OPERATION_FPUT)
			return;

	if (!vf_log_be)
		if (operation == VF_OPERATION_BPEEK ||
		    operation == VF_OPERATION_BGET ||
		    operation == VF_OPERATION_BPUT)
			return;

	if (log->log_cur >= VF_LOG_LEN) {
		return;
	}
	do_gettimeofday(&log->log_time[log->log_cur]);
	for (i = 0; i < 11; i++)
		log->log_buf[log->log_cur][i] = 0x00;
	for (i = 0; i < p->size; i++)
	{
		switch (p->master[i].status)
		{
		case VF_STATUS_WL:
			log->log_buf[log->log_cur][0] |= 1 << i;
			break;
		case VF_STATUS_WM:
			log->log_buf[log->log_cur][1] |= 1 << i;
			break;
		case  VF_STATUS_RL:
			log->log_buf[log->log_cur][2] |= 1 << i;
			break;
		case  VF_STATUS_RM:
			log->log_buf[log->log_cur][3] |= 1 << i;
			break;
		case  VF_STATUS_WT:
			log->log_buf[log->log_cur][4] |= 1 << i;
			break;
		default:
			break;
		}

		switch (p->slave[i].status)
		{
		case VF_STATUS_WL:
			log->log_buf[log->log_cur][5] |= 1 << i;
			break;
		case VF_STATUS_WM:
			log->log_buf[log->log_cur][6] |= 1 << i;
			break;
		case  VF_STATUS_RL:
			log->log_buf[log->log_cur][7] |= 1 << i;
			break;
		case  VF_STATUS_RM:
			log->log_buf[log->log_cur][8] |= 1 << i;
			break;
		case  VF_STATUS_WT:
			log->log_buf[log->log_cur][9] |= 1 << i;
			break;
		default:
			break;
		}
	}
	log->log_buf[log->log_cur][10] = operation;
	if (!operation_done)
	{
		log->log_buf[log->log_cur][10] |= 0x80;
	}
	log->log_cur++;
}
#else
static void vf_log(struct vf_pool *p, enum vf_operation_e operation, bool failure)
{
}
#endif

#ifdef VF_LOG_EN
void vf_log_print(struct vf_pool *p)
{
	unsigned int i = 0, j = 0, k = 0;
	int len = 0;
	char buf1[100];
	char buf2[100];
	struct vf_log_s *log = &p->log;

	pr_info("%-10s %-10s %-10s %-10s %-10s %-10s %5s\n",
			"WR_LIST", "RW_MODE", "RD_LIST", "RD_MODE", "WT_LIST",
			"OPERATIOIN", "TIME");

	for (i = 0; i < log->log_cur; i++)
	{
		memset(buf1, 0, sizeof(buf1));
		len = 0;

		for (j = 0; j < 5; j++)
		{
			for (k = 0; k < 8; k++)
			{
				if (k == 4)
					len += sprintf(buf1+len, "-");
				len += sprintf(buf1+len, "%1d",(log->log_buf[i][j] >> (7 - k)) & 1);
			}
			len += sprintf(buf1+len, "  ");
		}

		if (log->log_buf[i][10] & 0x80)
			len += sprintf(buf1+len, "(X)");
		else
			len += sprintf(buf1+len, "	 ");
		switch (log->log_buf[i][10] & 0x7)
		{
			case VF_OPERATION_INIT:
				len += sprintf(buf1+len, "%-7s", "INIT");
				break;
			case VF_OPERATION_FPEEK:
				len += sprintf(buf1+len, "%-7s", "FE_PEEK");
				break;
			case VF_OPERATION_FGET:
				len += sprintf(buf1+len, "%-7s", "FE_GET");
				break;
			case VF_OPERATION_FPUT:
				len += sprintf(buf1+len, "%-7s", "FE_PUT");
				break;
			case VF_OPERATION_BPEEK:
				len += sprintf(buf1+len, "%-7s", "BE_PEEK");
				break;
			case VF_OPERATION_BGET:
				len += sprintf(buf1+len, "%-7s", "BE_GET");
				break;
			case VF_OPERATION_BPUT:
				len += sprintf(buf1+len, "%-7s", "BE_PUT");
				break;
			case VF_OPERATION_FREE:
				len += sprintf(buf1+len, "%-7s", "FREE");
				break;
			default:
				break;
		}
		len += sprintf(buf1+len, " %ld.%03ld", (long)log->log_time[i].tv_sec,
			(long)log->log_time[i].tv_usec/1000);

		memset(buf2, 0, sizeof(buf2));
		len = 0;
		for (j = 5; j < 10; j++)
		{
			for (k = 0; k < 8; k++)
			{
				if (k == 4)
					len += sprintf(buf2 + len, "-");
				len += sprintf(buf2 + len, "%1d",(log->log_buf[i][j] >> (7 - k)) & 1);
			}
			len += sprintf(buf2 + len, "  ");
		}
		printk("%s\n", buf1);
		printk("%s\n\n", buf2);
	}
}
#else
void vf_log_print(struct vf_pool *p)
{
}
#endif
#ifdef ISR_LOG_EN
inline void isr_log_init(struct vf_pool *p)
{       
     memset(&p->isr_log, 0, sizeof(struct isr_log_s));
     p->isr_log.isr_log_en = 1;
     
}
inline void isr_log_print(struct vf_pool *p)
{
        unsigned int i = 0;
        struct isr_log_s *log = &p->isr_log;
        log->isr_log_en = 0;
        pr_info("%-7s\t%-7s\t%-7s\n",
			"ID", "ITIME", "OTIME");
        for(i=0; i<= log->log_cur; i+=2){
                pr_info("%u\t %7ld.%03ld\t%7ld.%03ld\n", i>>1, (long)log->isr_time[i].tv_sec,
	                (long)log->isr_time[i].tv_usec,(long)log->isr_time[i+1].tv_sec,
		        (long)log->isr_time[i+1].tv_usec);
        }
        
        
}
inline void isr_log(struct vf_pool *p)
{
        struct isr_log_s *log = &p->isr_log;
        if(!log->isr_log_en)
                return;
        if(log->log_cur >= ISR_LOG_LEN)
                return;
        do_gettimeofday(&log->isr_time[log->log_cur]);
        log->log_cur++;
}
#else
inline void isr_log_init(struct vf_pool *p)
{        
        
}
inline void isr_log_print(struct vf_pool *p)
{
        
}
inline void isr_log(struct vf_pool *p)
{
        
}
#endif
inline struct vf_entry *vf_get_master(struct vf_pool *p, int index)
{
	return &p->master[index];
}

inline struct vf_entry *vf_get_slave(struct vf_pool *p, int index)
{
	return &p->slave[index];
}

/* size : max canvas quantity */
struct vf_pool *vf_pool_alloc(int size)
{
	struct vf_pool *p;
	p = kmalloc(sizeof(struct vf_pool), GFP_KERNEL);
	if (!p) {
		return NULL;
	}
	p->master = kmalloc((sizeof(struct vf_entry) * size), GFP_KERNEL);
	if (!p->master) {
		kfree(p);
		return NULL;
	}
	p->slave  = kmalloc((sizeof(struct vf_entry) * size), GFP_KERNEL);
	if (!p->slave) {
		kfree(p->master);
		kfree(p);
		return NULL;
	}
	memset(p->master, 0, (sizeof(struct vf_entry) * size));
	memset(p->slave, 0, (sizeof(struct vf_entry) * size));
	p->max_size = size;
	/* initialize list lock */
	spin_lock_init(&p->wr_lock);
        spin_lock_init(&p->rd_lock);
        spin_lock_init(&p->wt_lock);
        spin_lock_init(&p->fz_lock);
	spin_lock_init(&p->tmp_lock);
	/* initialize list head */
	INIT_LIST_HEAD(&p->wr_list);
	INIT_LIST_HEAD(&p->rd_list);
	INIT_LIST_HEAD(&p->wt_list);
        INIT_LIST_HEAD(&p->fz_list);
	INIT_LIST_HEAD(&p->tmp_list);
	return p;
}

/* size: valid canvas quantity*/
int vf_pool_init(struct vf_pool *p, int size)
{
	int i = 0;
	unsigned long flags;
	struct vf_entry *master, *slave;
	struct vf_entry *pos = NULL, *tmp = NULL;

	if (!p)
		return -1;
	if (size > p->max_size)
		return -1;
	p->size = size;
        
        /*clear pool flag*/
        p->pool_flag = 0;
	/* clear write list */
	spin_lock_irqsave(&p->wr_lock, flags);
	list_for_each_entry_safe(pos, tmp, &p->wr_list, list) {
		list_del(&pos->list);
	}
	spin_unlock_irqrestore(&p->wr_lock, flags);

	/* clear read list */
	spin_lock_irqsave(&p->rd_lock, flags);
	list_for_each_entry_safe(pos, tmp, &p->rd_list, list) {
		list_del(&pos->list);
	}
	spin_unlock_irqrestore(&p->rd_lock, flags);

	spin_lock_irqsave(&p->wt_lock, flags);
	/* clear wait list */
	list_for_each_entry_safe(pos, tmp, &p->wt_list, list) {
		list_del(&pos->list);
	}
	spin_unlock_irqrestore(&p->wt_lock, flags);
        /* clear freeze list*/
        spin_lock_irqsave(&p->fz_lock, flags);
        list_for_each_entry_safe(pos, tmp, &p->fz_list, list) {
		list_del(&pos->list);
	}
	spin_unlock_irqrestore(&p->fz_lock,flags);
	/* clear tmp list*/
        spin_lock_irqsave(&p->tmp_lock, flags);
        list_for_each_entry_safe(pos, tmp, &p->tmp_list, list) {
		list_del(&pos->list);
	}
        spin_unlock_irqrestore(&p->tmp_lock,flags);
	p->wr_list_size = 0;
	p->rd_list_size = 0;
        p->fz_list_size = 0;
	p->tmp_list_size = 0;
	/* initialize provider write list */
	for (i = 0; i < size; i++) {
		master = vf_get_master(p, i);
		master->status = VF_STATUS_WL;
		master->flag |= VF_FLAG_NORMAL_FRAME;
                master->flag &= (~VF_FLAG_FREEZED_FRAME);
		spin_lock_irqsave(&p->wr_lock, flags);
		list_add(&master->list, &p->wr_list);
		p->wr_list_size++;
		spin_unlock_irqrestore(&p->wr_lock, flags);
	}

	for (i = 0; i < size; i++) {
		slave = vf_get_slave(p, i);
		slave->status = VF_STATUS_SL;
	}

#ifdef VF_LOG_EN
	vf_log_init(p);
	vf_log(p, VF_OPERATION_INIT, true);
#endif
	return 0;
}


/* free the vframe pool of the vfp */
void vf_pool_free(struct vf_pool *p)
{
	if (p) {
		vf_log(p, VF_OPERATION_FREE, true);
		if (p->master)
			kfree(p->master);
		if (p->slave)
			kfree(p->master);
		kfree(p);
	}
}

/* return the last entry */
static inline struct vf_entry *vf_pool_peek(struct list_head *head)
{
	struct vf_entry *vfe;
	if (list_empty(head))
		return NULL;
	vfe = list_entry(head->prev, struct vf_entry, list);
	return vfe;
}

/* return and del the last entry*/
static inline struct vf_entry *vf_pool_get(struct list_head *head)
{
	struct vf_entry *vfe;
	if (list_empty(head))
		return NULL;
	vfe = list_entry(head->prev, struct vf_entry, list);
	list_del(&vfe->list);
	return vfe;
}

/* add entry in the list head */
static inline void vf_pool_put(struct vf_entry *vfe, struct list_head *head)
{
	list_add(&vfe->list, head);
}
/*
*move all vf_entrys in tmp list to writable list
*/
void recycle_tmp_vfs(struct vf_pool *p)
{
	struct vf_entry *pos = NULL, *tmp = NULL;
	unsigned long flags;
	spin_lock_irqsave(&p->tmp_lock, flags);
	list_for_each_entry_safe(pos, tmp, &p->tmp_list, list) {
		list_del(&pos->list);
		receiver_vf_put(&pos->vf,p);
		p->tmp_list_size--;
	}
	spin_unlock_irqrestore(&p->tmp_lock, flags);
}
/*
*put vf_entry to tmp list
*/
void tmp_vf_put(struct vf_entry *vfe,struct vf_pool *p)
{
	unsigned long flags;
	spin_lock_irqsave(&p->tmp_lock, flags);
	vf_pool_put(vfe, &p->tmp_list);
	p->tmp_list_size++;
	spin_unlock_irqrestore(&p->tmp_lock, flags);
}
/*
*move all vf_entry in tmp list to readable list
*/
void tmp_to_rd(struct vf_pool *p)
{
	struct vf_entry *pos = NULL, *tmp = NULL;
	unsigned long flags;
	spin_lock_irqsave(&p->tmp_lock, flags);
	list_for_each_entry_safe(pos, tmp, &p->tmp_list, list) {
		list_del(&pos->list);
		provider_vf_put(pos,p);
		p->tmp_list_size--;
	}
	spin_unlock_irqrestore(&p->tmp_lock, flags);
}
inline struct vf_entry *provider_vf_peek(struct vf_pool *p)
{
	struct vf_entry *vfe;
	unsigned long flags;

	spin_lock_irqsave(&p->wr_lock, flags);
	vfe = vf_pool_peek(&p->wr_list);
	spin_unlock_irqrestore(&p->wr_lock, flags);
	if (!vfe)
		vf_log(p, VF_OPERATION_FPEEK, false);
	else
		vf_log(p, VF_OPERATION_FPEEK, true);
	return vfe;
}

/* provider get last vframe to write */
inline struct vf_entry *provider_vf_get(struct vf_pool *p)
{
	struct vf_entry *vfe;
	unsigned long flags;

	spin_lock_irqsave(&p->wr_lock, flags);
	vfe = vf_pool_get(&p->wr_list);
	spin_unlock_irqrestore(&p->wr_lock, flags);
	if (!vfe) {
		vf_log(p, VF_OPERATION_FGET, false);
		return NULL;
	}
	p->wr_list_size--;
	vfe->status = VF_STATUS_WM;
	vf_log(p, VF_OPERATION_FGET, true);
	return vfe;
}

inline void provider_vf_put(struct vf_entry *vfe, struct vf_pool *p)
{
	unsigned long flags;

	spin_lock_irqsave(&p->rd_lock, flags);
	vfe->status = VF_STATUS_RL;
	vf_pool_put(vfe, &p->rd_list);
	p->rd_list_size++;
	spin_unlock_irqrestore(&p->rd_lock, flags);
	vf_log(p, VF_OPERATION_FPUT, true);
}

/* receiver peek to read */
inline struct vf_entry *receiver_vf_peek(struct vf_pool *p)
{
	struct vf_entry *vfe;
	unsigned long flags;
        
        if(p->pool_flag & VDIN_VF_POOL_FREEZE){
                spin_lock_irqsave(&p->fz_lock, flags);
                vfe = vf_pool_peek(&p->fz_list);
                spin_unlock_irqrestore(&p->fz_lock, flags);
                return vfe;
        }
                
	spin_lock_irqsave(&p->rd_lock, flags);
	vfe = vf_pool_peek(&p->rd_list);
	spin_unlock_irqrestore(&p->rd_lock, flags);
	if (!vfe)
		vf_log(p, VF_OPERATION_BPEEK, false);
	else
		vf_log(p, VF_OPERATION_BPEEK, true);
	if (!vfe)
		return NULL;                
	return vfe;
}

/* receiver get vframe to read */
inline struct vf_entry *receiver_vf_get(struct vf_pool *p)
{
	struct vf_entry *vfe;
	unsigned long flags;
        /*get the vframe from the frozen list*/
        if(p->pool_flag & VDIN_VF_POOL_FREEZE){
                spin_lock_irqsave(&p->fz_lock, flags);
                if(list_empty(&p->fz_list)){
                       spin_unlock_irqrestore(&p->fz_lock, flags);
		       return NULL;
                }
                vfe = vf_pool_get(&p->fz_list);
                spin_unlock_irqrestore(&p->fz_lock, flags);
                return vfe;
        }

	spin_lock_irqsave(&p->rd_lock, flags);
	if (list_empty(&p->rd_list)) {
		spin_unlock_irqrestore(&p->rd_lock, flags);
		vf_log(p, VF_OPERATION_BGET, false);
		return NULL;
	}
        
	vfe = vf_pool_get(&p->rd_list);
	p->rd_list_size--;
	spin_unlock_irqrestore(&p->rd_lock, flags);
	vfe->status = VF_STATUS_RM;
                
	vf_log(p, VF_OPERATION_BGET, true);
	return vfe;
}

inline void receiver_vf_put(struct vframe_s *vf, struct vf_pool *p)
{
	struct vf_entry *master, *slave;
	unsigned long flags;
	struct vf_entry *pos = NULL, *tmp = NULL;
	int found_in_wt_list = 0;
        

	master = vf_get_master(p, vf->index);
        
        /*keep the frozen frame in rd list&recycle the frame which not in fz list when unfreeze*/
        if(master->flag & VF_FLAG_FREEZED_FRAME){
                if(p->pool_flag & VDIN_VF_POOL_FREEZE ){
                        spin_lock_irqsave(&p->fz_lock, flags);
                        vf_pool_put(master, &p->fz_list);
                        spin_unlock_irqrestore(&p->fz_lock, flags);
                        return;
                }
                master->flag &= (~VF_FLAG_FREEZED_FRAME);
                pr_info("%s: del frame %u from fz list.\n",__func__,master->vf.index);
        }
                
	/* normal vframe */
	if (master->flag & VF_FLAG_NORMAL_FRAME) {
		master->status = VF_STATUS_WL;
		spin_lock_irqsave(&p->wr_lock, flags);
		vf_pool_put(master, &p->wr_list);
		p->wr_list_size++;
		spin_unlock_irqrestore(&p->wr_lock, flags);
		vf_log(p, VF_OPERATION_BPUT, true);
	}
	else {
		spin_lock_irqsave(&p->wt_lock, flags);
		list_for_each_entry_safe(pos, tmp, &p->wt_list, list) {
			/*
			 * if the index to be putted is same with one in wt_list,
			 * we consider that they are the same entry.
			 * so the same vfrmae hold by receiver should not be putted
			 * more than one times.
			 */
			if (pos->vf.index == vf->index) {
				found_in_wt_list = 1;
				break;
			}
		}
		/*
		 * the entry 'pos' found in wt_list maybe entry 'master' or 'slave'
		 */
		slave = vf_get_slave(p, vf->index);
		/* if found associated entry in wait list */
		if (found_in_wt_list) {
			/* remove from wait list, and put the master entry in wr_list */
			list_del(&pos->list);
			spin_unlock_irqrestore(&p->wt_lock, flags);

			master->status = VF_STATUS_WL;
			spin_lock_irqsave(&p->wr_lock, flags);
			vf_pool_put(master, &p->wr_list);
			p->wr_list_size++;
			spin_unlock_irqrestore(&p->wr_lock, flags);
			slave->status = VF_STATUS_SL;
			vf_log(p, VF_OPERATION_BPUT, true);
		}
		/* if not found associated entry in wait list */
		else {
			/*
			 * put the recycled vframe in wait list
			 *
			 * you should put the vframe that you getted, you should not put
			 * the copy one. because we also compare the address of vframe
			 * structure to determine it is master or slave.
			 */
			if (&slave->vf == vf) {
				slave->status = VF_STATUS_WT;
				list_add(&slave->list, &p->wt_list);
			}
			else {
				master->status = VF_STATUS_WT;
				list_add(&master->list, &p->wt_list);
			}
			spin_unlock_irqrestore(&p->wt_lock, flags);
			vf_log(p, VF_OPERATION_BPUT, true);
		}
	}
}

struct vframe_s *vdin_vf_peek(void* op_arg)
{
	struct vf_pool *p;
	struct vf_entry *vfe;
	if (!op_arg)
		return NULL;
	p = (struct vf_pool*)op_arg;
	vfe =  receiver_vf_peek(p);
	if (!vfe)
		return NULL;
	return &vfe->vf;
}

struct vframe_s *vdin_vf_get (void* op_arg)
{
	struct vf_pool *p;
	struct vf_entry *vfe;
	if (!op_arg)
		return NULL;
	p = (struct vf_pool*)op_arg;
	vfe =  receiver_vf_get(p);
	if (!vfe)
		return NULL;
	return &vfe->vf;
}

void vdin_vf_put(struct vframe_s *vf, void* op_arg)
{
	struct vf_pool *p;
	if (!op_arg)
		return;
	p = (struct vf_pool*)op_arg;
	receiver_vf_put(vf, p);
}
int vdin_vf_states(vframe_states_t *vf_ste, void* op_arg)
{
	struct vf_pool *p;
	if(!vf_ste)
		return -1;
	p = (struct vf_pool*)op_arg;
	vf_ste->vf_pool_size = p->size;
	vf_ste->buf_free_num = p->wr_list_size - 1;
	vf_ste->buf_avail_num = p->wr_list_size;
	vf_ste->buf_recycle_num = p->size - p->wr_list_size - p->rd_list_size;
	return 0;
}

/*
*hold the buffer from rd list,if rd list is not enough,
*get buffer from wr list
*/
void vdin_vf_freeze(struct vf_pool *p, unsigned int num)
{
        struct vf_entry *vfe,*tmp;
        struct list_head *rd_head, *wr_head,*fz_head;
        unsigned long flags;
        rd_head  = &p->rd_list;
        wr_head = &p->wr_list;
        fz_head  = &p->fz_list;
        p->pool_flag |= VDIN_VF_POOL_FREEZE;
        if(p->fz_list_size < num){
                /*add the buffer in rd list to fz list*/
                spin_lock_irqsave(&p->rd_lock, flags);
                list_for_each_entry_safe(vfe, tmp, rd_head, list){
                        list_del(&vfe->list);
                        spin_lock_irqsave(&p->fz_lock, flags);
                        list_add_tail(&vfe->list,fz_head);
                        spin_unlock_irqrestore(&p->fz_lock, flags);                                        
                        vfe->flag |=  VF_FLAG_FREEZED_FRAME;
                        pr_info("%s: add  %u frame from rd list.\n",__func__,vfe->vf.index);
                        if(++p->fz_list_size >= num)
                                break;
                }
                spin_unlock_irqrestore(&p->rd_lock, flags);
                if(p->fz_list_size < num){            
                        spin_lock_irqsave(&p->wr_lock, flags);
                        spin_lock_irqsave(&p->fz_lock, flags);
                        list_for_each_entry_safe_reverse(vfe, tmp, wr_head, list){
                                list_del(&vfe->list);                                
                                list_add(&vfe->list,fz_head);                                                                    
                                vfe->flag |=  VF_FLAG_FREEZED_FRAME;
                                pr_info("%s: add frame %u from wr list.\n",__func__,vfe->vf.index);
                                if(++p->fz_list_size >= num)
                                        break;
                       }
                        spin_unlock_irqrestore(&p->fz_lock, flags);    
                       spin_unlock_irqrestore(&p->wr_lock, flags);

                 }                

        }
}

void vdin_vf_unfreeze(struct vf_pool *p)
{
        struct list_head *fz_head, *wr_head;
        struct vf_entry *vfe, *tmp;
        unsigned long flags;
        fz_head = &p->fz_list;
        wr_head = &p->wr_list;
        if(p->fz_list_size > 0){
                p->pool_flag &= (~VDIN_VF_POOL_FREEZE);
                p->fz_list_size = 0;
                spin_lock_irqsave(&p->fz_lock, flags);
                spin_lock_irqsave(&p->wr_lock, flags);
                list_for_each_entry_safe(vfe, tmp, fz_head, list){
                        list_del(&vfe->list);
                        list_add_tail(&vfe->list,wr_head);
                        vfe->flag &=  (~VF_FLAG_FREEZED_FRAME);
                pr_info("%s: del frame %u from fz list.\n",__func__,vfe->vf.index);
                }
                spin_unlock_irqrestore(&p->wr_lock, flags);
                spin_unlock_irqrestore(&p->fz_lock, flags);
        }        
}

void vdin_dump_vf_state(struct vf_pool *p)
{
        unsigned long flags;
        struct vf_entry *pos = NULL, *tmp = NULL;

	pr_info("buffers in writeable list:\n");
        spin_lock_irqsave(&p->wr_lock, flags);
	list_for_each_entry_safe(pos, tmp, &p->wr_list, list){
		pr_info("\t index: %2u,status %u, canvas index0: 0x%x, index1: 0x%x, vframe type: 0x%x.\n",
			pos->vf.index,pos->status,pos->vf.canvas0Addr,pos->vf.canvas1Addr,pos->vf.type);
	}
        spin_unlock_irqrestore(&p->wr_lock, flags);

	pr_info("buffer in readable list:\n");
        spin_lock_irqsave(&p->rd_lock, flags);
	list_for_each_entry_safe(pos, tmp, &p->rd_list, list){
		pr_info("\t index: %u,status %u, canvas index0: 0x%x, index1: 0x%x, vframe type: 0x%x.\n",
		        pos->vf.index,pos->status,pos->vf.canvas0Addr,pos->vf.canvas1Addr,pos->vf.type);
	}
	spin_unlock_irqrestore(&p->rd_lock,flags);

	pr_info("buffer in waiting list:\n");
	spin_lock_irqsave(&p->wt_lock, flags);
        list_for_each_entry_safe(pos, tmp, &p->wt_list, list){
		pr_info("\t index: %u, status %u, canvas index0: 0x%x, index1: 0x%x, vframe type: 0x%x.\n",
			pos->vf.index,pos->status,pos->vf.canvas0Addr,pos->vf.canvas1Addr,pos->vf.type);
	}
        spin_unlock_irqrestore(&p->wt_lock, flags);
	pr_info("buffer in temp list:\n");
	spin_lock_irqsave(&p->tmp_lock, flags);
	list_for_each_entry_safe(pos, tmp, &p->tmp_list, list){
		pr_info("\t index: %u, status %u, canvas index0: 0x%x, index1: 0x%x, vframe type: 0x%x.\n",
			pos->vf.index,pos->status,pos->vf.canvas0Addr,pos->vf.canvas1Addr,pos->vf.type);
	}
	spin_unlock_irqrestore(&p->tmp_lock, flags);
		
}

