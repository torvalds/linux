/*
 * Video Frame Manager For Provider and Receiver
 *
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
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>

/* Local headers */
#include "vfm.h"

#define MAX_PROVIDER_NUM    32
vframe_provider_t* provider_table[MAX_PROVIDER_NUM];

int provider_list(char* buf)
{
    struct vframe_provider_s *p = NULL;
    int len = 0;
    int i;
    len += sprintf(buf+len, "\nprovider list:\n");
    for(i=0; i<MAX_PROVIDER_NUM; i++){
        p = provider_table[i];
        if(p){
            len += sprintf(buf+len, "   %s\n", p->name);
        }
    }
    return len;
}
/*
 * get vframe provider from the provider list by receiver name.
 *
 */
struct vframe_provider_s * vf_get_provider_by_name(const char *provider_name)
{
    struct vframe_provider_s *p = NULL;
    int i;
    if(provider_name){
        int namelen = strlen(provider_name);
        for(i=0; i<MAX_PROVIDER_NUM; i++){
            p = provider_table[i];
            if(p){
                if (!strncmp(p->name, provider_name, namelen)) {
                    if( strlen(p->name) == namelen ||
                        p->name[namelen] == '.'){
                        break;
                    }
                }
            }
        }
        if(i == MAX_PROVIDER_NUM){
            p = NULL;
        } 
    }
    return p;
}
 
struct vframe_provider_s * vf_get_provider(const char *receiver_name)
{
    struct vframe_provider_s *p = NULL;
    char* provider_name;

    provider_name = vf_get_provider_name( receiver_name);
    
    p = vf_get_provider_by_name(provider_name);
    return p;
}
EXPORT_SYMBOL(vf_get_provider);


int vf_notify_provider(const char* receiver_name, int event_type, void* data)
{
    int ret = -1;
    struct vframe_provider_s * provider = vf_get_provider(receiver_name);
    if(provider){
        if(provider->ops && provider->ops->event_cb){
            provider->ops->event_cb(event_type, data, provider->op_arg);
            ret = 0;
        }
    }
    else{
        //pr_err("Error: %s, fail to get provider of receiver %s\n", __func__, receiver_name);
    }
    return ret;
}
EXPORT_SYMBOL(vf_notify_provider);

void vf_provider_init(struct vframe_provider_s *prov,
    const char *name, const struct vframe_operations_s *ops, void* op_arg)
{
    if (!prov)
        return;
    //memset(prov, 0, sizeof(struct vframe_provider_s));
    prov->name = name;
    prov->ops = ops;
    prov->op_arg = op_arg;
    INIT_LIST_HEAD(&prov->list);
}

EXPORT_SYMBOL(vf_provider_init);

int vf_reg_provider(struct vframe_provider_s *prov)
{
    vframe_provider_t *p = NULL;
    vframe_receiver_t* receiver = NULL;
    int i;

    if (!prov || !prov->name)
        return -1;

    for(i=0; i<MAX_PROVIDER_NUM; i++){
        p = provider_table[i];
        if(p){
            if (!strcmp(p->name, prov->name)) {
                return -1;
            }
        }
    }
    for(i=0; i<MAX_PROVIDER_NUM; i++){
        if(provider_table[i] == NULL){
           provider_table[i] = prov; 
           break;
        }
    }

    if(i<MAX_PROVIDER_NUM){
        vf_update_active_map();
        receiver = vf_get_receiver(prov->name);
        if(receiver && receiver->ops && receiver->ops->event_cb){
            receiver->ops->event_cb(VFRAME_EVENT_PROVIDER_REG, (void *)prov->name, receiver->op_arg);
        }
        else{
            printk("%s Error to notify receiver\n", __func__);
        }
        if(vfm_debug_flag&1){
            printk("%s:%s\n", __func__, prov->name);
        }
    }
    else{
        printk("%s: Error, provider_table full\n", __func__);
    }
    return 0;
}

EXPORT_SYMBOL(vf_reg_provider);

void vf_unreg_provider(struct vframe_provider_s *prov)
{
    vframe_provider_t *p = NULL; 
    vframe_receiver_t* receiver = NULL;
    int i;
    for(i=0; i<MAX_PROVIDER_NUM; i++){
        p = provider_table[i];
        if(p){
            if (!strcmp(p->name, prov->name)) {
                provider_table[i] = NULL;
                if(vfm_debug_flag&1){
                    printk("%s:%s\n", __func__, prov->name);
                }
                receiver = vf_get_receiver(prov->name);
                if(receiver && receiver->ops && receiver->ops->event_cb){
                    receiver->ops->event_cb(VFRAME_EVENT_PROVIDER_UNREG, NULL, receiver->op_arg);
                }
                else{
                    printk("%s Error to notify receiver\n", __func__);
                }
                vf_update_active_map();
                break;
            }
        }
    }

}
EXPORT_SYMBOL(vf_unreg_provider);

void vf_light_unreg_provider(struct vframe_provider_s *prov)
{
    vframe_provider_t *p = NULL; 
    vframe_receiver_t* receiver = NULL;
    int i;
    for(i=0; i<MAX_PROVIDER_NUM; i++){
        p = provider_table[i];
        if(p){
            if (!strcmp(p->name, prov->name)) {
                if(vfm_debug_flag&1){
                    printk("%s:%s\n", __func__, prov->name);
                }
                receiver = vf_get_receiver(p->name);
                if(receiver && receiver->ops && receiver->ops->event_cb){
                    receiver->ops->event_cb(VFRAME_EVENT_PROVIDER_LIGHT_UNREG, NULL, receiver->op_arg);
                }
                break;
            }
        }
    }
}
EXPORT_SYMBOL(vf_light_unreg_provider);

void vf_ext_light_unreg_provider(struct vframe_provider_s *prov)
{
    vframe_provider_t *p = NULL; 
    vframe_receiver_t* receiver = NULL;
    int i;
    for(i=0; i<MAX_PROVIDER_NUM; i++){
        p = provider_table[i];
        if(p){
            if (!strcmp(p->name, prov->name)) {
                provider_table[i] = NULL;
                if(vfm_debug_flag&1){
                    printk("%s:%s\n", __func__, prov->name);
                }
                receiver = vf_get_receiver(prov->name);
                if(receiver && receiver->ops && receiver->ops->event_cb){
                    receiver->ops->event_cb(VFRAME_EVENT_PROVIDER_LIGHT_UNREG, NULL, receiver->op_arg);
                }
                vf_update_active_map();
                break;
            }
        }
    }
}
EXPORT_SYMBOL(vf_ext_light_unreg_provider);

struct vframe_s *vf_peek(const char* receiver)
{
    struct vframe_provider_s *vfp;
    vfp = vf_get_provider(receiver);
    if (!(vfp && vfp->ops && vfp->ops->peek))
        return NULL;
    return vfp->ops->peek(vfp->op_arg);
}
EXPORT_SYMBOL(vf_peek);

struct vframe_s *vf_get(const char* receiver)
{
    struct vframe_provider_s *vfp;
    vfp = vf_get_provider(receiver);
    if (!(vfp && vfp->ops && vfp->ops->get))
        return NULL;
    return vfp->ops->get(vfp->op_arg);
}
EXPORT_SYMBOL(vf_get);

void vf_put(struct vframe_s *vf, const char *receiver)
{
    struct vframe_provider_s *vfp;
    vfp = vf_get_provider(receiver);
    if (!(vfp && vfp->ops && vfp->ops->put))
        return;
    vfp->ops->put(vf, vfp->op_arg);
}
EXPORT_SYMBOL(vf_put);



