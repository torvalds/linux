/*
 * Video Frame Manager For Provider and Receiver
 *
 * Author: Bobby Yang <bo.yang@amlogic.com>
 *
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Standard Linux headers */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>

/* Amlogic headers */
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_receiver.h>

/* Local headers */
#include "vfm.h"

#define MAX_RECEIVER_NUM    8
vframe_receiver_t* receiver_table[MAX_RECEIVER_NUM];

int receiver_list(char* buf)
{
    struct vframe_receiver_s *r = NULL;
    int len = 0;
    int i = 0;
    len += sprintf(buf+len, "\nreceiver list:\n");
    for(i=0; i<MAX_RECEIVER_NUM; i++){
        r = receiver_table[i];
        if(r){
            len += sprintf(buf+len, "   %s\n", r->name);
        }
    }
    return len;
}
/*
 * get vframe provider from the provider list by receiver name.
 *
 */
struct vframe_receiver_s * vf_get_receiver(const char *provider_name)
{
    struct vframe_receiver_s *r = NULL;
    char* receiver_name = NULL;
    int i;
    receiver_name = vf_get_receiver_name( provider_name);
    if(vfm_debug_flag&2){
        printk("%s:receiver_name:%s\n", __func__, receiver_name);
    }
    if(receiver_name){
        for(i=0; i<MAX_RECEIVER_NUM; i++){
            r = receiver_table[i];
            if(r){
                if(vfm_debug_flag&2){
                    printk("%s: r: %s\n", __func__, r->name);
                }
                if (!strcmp(r->name, receiver_name)) {
                        break;
                }
            }
        }
        if(i == MAX_RECEIVER_NUM){
            r = NULL;
        }
    }
    return r;
}
EXPORT_SYMBOL(vf_get_receiver);

struct vframe_receiver_s * vf_get_receiver_by_name(const char *receiver_name)
{
    vframe_receiver_t *r = NULL;
    int i = 0;
    if(receiver_name){
        for(i=0; i<MAX_RECEIVER_NUM; i++){
            r = receiver_table[i];
            if(r){
                if(vfm_debug_flag&2){
                    printk("%s: r: %s\n", __func__, r->name);
                }
                if (!strcmp(r->name, receiver_name)) {
                        break;
                }
            }
        }
        if(i == MAX_RECEIVER_NUM){
            r = NULL;
        }
    }
    return r;
}

int vf_notify_receiver(const char* provider_name, int event_type, void* data)
{
    int ret = -1;
    vframe_receiver_t* receiver = NULL;

    receiver = vf_get_receiver(provider_name);
    if(receiver){
        if(receiver->ops && receiver->ops->event_cb){
            ret = receiver->ops->event_cb(event_type, data, receiver->op_arg);
        }
    }
    else{
        pr_err("Error: %s, fail to get receiver of provider %s\n", __func__, provider_name);
    }
    return ret;
}
EXPORT_SYMBOL(vf_notify_receiver);

int vf_notify_receiver_by_name(const char* receiver_name, int event_type, void* data)
{
    int ret = -1;
    vframe_receiver_t* receiver = NULL;

    receiver = vf_get_receiver_by_name(receiver_name);
    if(receiver){
        if(receiver->ops && receiver->ops->event_cb){
            ret = receiver->ops->event_cb(event_type, data, receiver->op_arg);
        }
    }
    return ret;
}
EXPORT_SYMBOL(vf_notify_receiver_by_name);


void vf_receiver_init(struct vframe_receiver_s *recv,
    const char *name, const struct vframe_receiver_op_s *ops, void* op_arg)
{
    if (!recv)
        return;
    memset(recv, 0, sizeof(struct vframe_receiver_s));
    recv->name = name;
    recv->ops = ops;
    recv->op_arg = op_arg;
    INIT_LIST_HEAD(&recv->list);
}
EXPORT_SYMBOL(vf_receiver_init);

int vf_reg_receiver(struct vframe_receiver_s *recv)
{
    vframe_receiver_t *r;
    int i;
    if (!recv)
        return -1;

    for(i=0; i<MAX_RECEIVER_NUM; i++){
        r = receiver_table[i];
        if(r){
            if (!strcmp(r->name, recv->name)) {
                return -1;
            }
        }
    }
    for(i=0; i<MAX_RECEIVER_NUM; i++){
        if(receiver_table[i] == NULL){
           receiver_table[i] = recv;
           break;
        }
    }
    return 0;
}
EXPORT_SYMBOL(vf_reg_receiver);

void vf_unreg_receiver(struct vframe_receiver_s *recv)
{
    vframe_receiver_t *r;
    int i;
    if (!recv)
        return;
    for(i=0; i<MAX_RECEIVER_NUM; i++){
        r = receiver_table[i];
        if(r){
            if (!strcmp(r->name, recv->name)) {
                receiver_table[i] = NULL;
                break;
            }
        }
    }
}
EXPORT_SYMBOL(vf_unreg_receiver);



