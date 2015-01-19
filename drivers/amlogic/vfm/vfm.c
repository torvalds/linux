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
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>

/* Amlogic headers */
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>

/* Local headers */
#include "vfm.h"

#define DRV_NAME    "vfm"
#define DEV_NAME    "vfm"
#define BUS_NAME    "vfm"
#define CLS_NAME    "vfm"

#define VFM_NAME_LEN    100
#define VFM_MAP_SIZE    10
#define VFM_MAP_COUNT   20
typedef struct{
    char id[VFM_NAME_LEN];
    char name[VFM_MAP_SIZE][VFM_NAME_LEN];
    int vfm_map_size;
    int valid;
    int active;
}vfm_map_t;

vfm_map_t* vfm_map[VFM_MAP_COUNT];
static int vfm_map_num = 0;

int vfm_debug_flag = 0; //1;

void vf_update_active_map(void)
{
    int i,j;
    struct vframe_provider_s *vfp;
    for (i = 0; i < vfm_map_num; i++) {
        if (vfm_map[i] && vfm_map[i]->valid) {
            for (j = 0; j < (vfm_map[i]->vfm_map_size - 1); j++) {
                vfp = vf_get_provider_by_name(vfm_map[i]->name[j]);
                if(vfp == NULL){
                    vfm_map[i]->active&=(~(1<<j));
                }
                else{
                    vfm_map[i]->active|=(1<<j);
                }
            }
        }
    }

}

static int get_vfm_map_index(const char* id)
{
    int index = -1;
    int i;
    for (i = 0; i < vfm_map_num; i++){
        if(vfm_map[i]){
            if (vfm_map[i]->valid && (!strcmp(vfm_map[i]->id, id))){
                index = i;
                break;
            }
        }
    }
    return index;
}

static int vfm_map_remove_by_index(int index)
{
    int i;
    int ret = 0;
    struct vframe_provider_s *vfp;

    vfm_map[index]->active = 0;
    for (i = 0; i < (vfm_map[index]->vfm_map_size - 1); i++) {
        vfp = vf_get_provider_by_name(vfm_map[index]->name[i]);
        if(vfp && vfp->ops && vfp->ops->event_cb){
            vfp->ops->event_cb(VFRAME_EVENT_RECEIVER_FORCE_UNREG, NULL, vfp->op_arg);
            printk("%s: VFRAME_EVENT_RECEIVER_FORCE_UNREG %s\n", __func__, vfm_map[index]->name[i]);
        }
    }

    for (i = 0; i < (vfm_map[index]->vfm_map_size - 1); i++) {
        vfp = vf_get_provider_by_name(vfm_map[index]->name[i]);
        if(vfp){
            break;
        }
    }
    if (i < (vfm_map[index]->vfm_map_size -1)) {
        pr_err("failed to remove vfm map %s with active provider %s.\n",
            vfm_map[index]->id, vfm_map[index]->name[i]);
        ret = -1;
    }
    vfm_map[index]->valid = 0;

    return ret;

}

static int vfm_map_remove(char* id)
{
    int i;
    int index;
    int ret = 0;
    if (!strcmp(id, "all")) {
        for (i = 0; i < vfm_map_num; i++) {
            if (vfm_map[i]) {
                ret = vfm_map_remove_by_index(i);
            }
        }
    }
    else{
        index = get_vfm_map_index(id);
        if (index >= 0) {
            ret = vfm_map_remove_by_index(index);
        }
    }
    return ret;
}

static int vfm_map_add(char* id,   char* name_chain)
{
    int i,j;
    int ret = -1;
    char* ptr, *token;
    vfm_map_t *p;

    p = kmalloc(sizeof(vfm_map_t), GFP_KERNEL);
    if (p) {
        memset(p, 0, sizeof(vfm_map_t));
        memcpy(p->id, id, strlen(id));
        p->valid = 1;
        ptr = name_chain;
        while (1) {
            token = strsep(&ptr, " \n");
            if (token == NULL)
                break;
            if (*token == '\0')
                continue;
            memcpy(p->name[p->vfm_map_size],
                token, strlen(token));
            p->vfm_map_size++;
        }
        for (i = 0; i < vfm_map_num; i++) {
            if (vfm_map[i]){
                if((vfm_map[i]->vfm_map_size == p->vfm_map_size)&&
                    (!strcmp(vfm_map[i]->id, p->id))){
                    for(j=0; j<p->vfm_map_size; j++){
                        if(strcmp(vfm_map[i]->name[j], p->name[j])){
                            break;
                        }
                    }
                    if(j == p->vfm_map_size){
                        vfm_map[i]->valid = 1;
                        kfree(p);
                        break;
                    }
                }
            }
        }

        if(i == vfm_map_num){
            if(i<VFM_MAP_COUNT){
                vfm_map[i] = p;
                vfm_map_num++;
            }
            else{
                printk("%s: Error, vfm_map full\n", __func__);
                ret = -1;
            }
        }

        ret = 0;

    }
    return ret;

}

char* vf_get_provider_name(const char* receiver_name)
{
    int i,j;
    char* provider_name = NULL;
    for (i = 0; i < vfm_map_num; i++) {
        if (vfm_map[i] && vfm_map[i]->active) {
            for (j = 0; j < vfm_map[i]->vfm_map_size; j++) {
                if (!strcmp(vfm_map[i]->name[j], receiver_name)) {
                    if ((j > 0)&&((vfm_map[i]->active>>(j-1))&0x1)) {
                        provider_name = vfm_map[i]->name[j - 1];
                    }
                    break;
                }
            }
        }
        if (provider_name)
            break;
    }
    return provider_name;
}

char* vf_get_receiver_name(const char* provider_name)
{
    int i,j;
    char* receiver_name = NULL;
    int namelen;
    int provide_namelen = strlen(provider_name);

    for (i = 0; i < vfm_map_num; i++) {
        if (vfm_map[i] && vfm_map[i]->valid) {
            for (j = 0; j < vfm_map[i]->vfm_map_size; j++){
                namelen = strlen(vfm_map[i]->name[j]);
                if(vfm_debug_flag&2){
                    printk("%s:vfm_map:%s\n", __func__, vfm_map[i]->name[j]);
                }
                if (!strncmp(vfm_map[i]->name[j], provider_name, namelen)) {
                    if (namelen == provide_namelen ||
                        provider_name[namelen] == '.') {
                        if ((j + 1) < vfm_map[i]->vfm_map_size) {
                            receiver_name = vfm_map[i]->name[j + 1];
                        }
                        break;
                    }
                }
            }
        }
        if (receiver_name)
            break;
    }

    return receiver_name;
}

static void vfm_init(void)
{

#ifdef CONFIG_POST_PROCESS_MANAGER
    char def_id[] = "default";
    char def_name_chain[] = "decoder ppmgr amvideo";
#else
    char def_id[] = "default";
    char def_name_chain[] = "decoder amvideo";
#endif
#ifdef CONFIG_TVIN_VIUIN
    char def_ext_id[] = "default_ext";
    char def_ext_name_chain[] = "vdin amvideo2";
#else
#ifdef CONFIG_AMLOGIC_VIDEOIN_MANAGER
    char def_ext_id[] = "default_ext";
#ifdef CONFIG_AMLOGIC_VM_DISABLE_VIDEOLAYER
    char def_ext_name_chain[] = "vdin0 vm";
#else
    char def_ext_name_chain[] = "vdin0 vm amvideo";
#endif
#endif
#endif
    char def_osd_id[] = "default_osd";
    char def_osd_name_chain[] = "osd amvideo4osd";
    //char def_osd_name_chain[] = "osd amvideo";

#ifdef CONFIG_VDIN_MIPI
    char def_mipi_id[] = "default_mipi";
    char def_mipi_name_chain[] = "vdin mipi";
#endif

#ifdef CONFIG_V4L_AMLOGIC_VIDEO2
    char def_amlvideo2_id[] = "default_amlvideo2";
    char def_amlvideo2_chain[] = "vdin1 amlvideo2";
#endif

#if (defined CONFIG_TVIN_AFE)||(defined CONFIG_TVIN_HDMI)
	char tvpath_id[] = "tvpath";
	char tvpath_chain[]="vdin0 deinterlace amvideo";
#endif
    int i;
    for(i=0; i<VFM_MAP_COUNT; i++){
        vfm_map[i] = NULL;
    }
    vfm_map_add(def_osd_id, def_osd_name_chain);
    vfm_map_add(def_id, def_name_chain);
#ifdef CONFIG_VDIN_MIPI
    vfm_map_add(def_mipi_id, def_mipi_name_chain);
#endif
#ifdef CONFIG_AMLOGIC_VIDEOIN_MANAGER
    vfm_map_add(def_ext_id, def_ext_name_chain);
#endif
#if (defined CONFIG_TVIN_AFE)||(defined CONFIG_TVIN_HDMI)
    vfm_map_add(tvpath_id, tvpath_chain);
#endif
#ifdef CONFIG_V4L_AMLOGIC_VIDEO2
    vfm_map_add(def_amlvideo2_id, def_amlvideo2_chain);
#endif
}

/*
 * cat /sys/class/vfm/map
*/
static ssize_t vfm_map_show(struct class *class,
    struct class_attribute *attr, char *buf)
{
    int i,j;
    int len = 0;

    for (i = 0; i < vfm_map_num; i++){
        if (vfm_map[i] && vfm_map[i]->valid){
            len += sprintf(buf+len, "%s { ", vfm_map[i]->id);
            for (j = 0; j < vfm_map[i]->vfm_map_size; j++){
                if (j < (vfm_map[i]->vfm_map_size-1)){
                    len += sprintf(buf+len, "%s(%d) ", vfm_map[i]->name[j], (vfm_map[i]->active>>j)&0x1);
                }
                else{
                    len += sprintf(buf+len, "%s", vfm_map[i]->name[j]);
                }
            }
            len += sprintf(buf+len, "}\n");
        }
    }
    len += provider_list(buf+len);
    len += receiver_list(buf+len);
    return len;
}

#define VFM_CMD_ADD 1
#define VFM_CMD_RM  2

/*
 * echo add <name> <node1 node2 ...> > /sys/class/vfm/map
 * echo rm <name>                    > /sys/class/vfm/map
 * echo rm all                       > /sys/class/vfm/map
 *
 * <name> the name of the path.
 * <node1 node2 ...> the name of the nodes in the path.
*/
static ssize_t vfm_map_store(struct class *class,
    struct class_attribute *attr, const char *buf, size_t count)
{
    char *buf_orig, *ps, *token;
    int i = 0;
    int cmd = 0;
    char* id = NULL;

  if(vfm_debug_flag&0x10000){
    return count;
  }
  printk("%s:%s\n", __func__, buf);

	buf_orig = kstrdup(buf, GFP_KERNEL);
	ps = buf_orig;
	while (1) {
		token = strsep(&ps, " \n");
		if (token == NULL)
			break;
		if (*token == '\0')
			continue;
	  if (i == 0){ //command
	      if (!strcmp(token, "add")){

            cmd = VFM_CMD_ADD;
	      }
	      else if (!strcmp(token, "rm")){
            cmd = VFM_CMD_RM;
	      }
	      else{
	        break;
	      }
	  }
	  else if (i == 1){
	      id = token;
    	    if (cmd == VFM_CMD_ADD){
    	        //printk("vfm_map_add(%s,%s)\n",id,ps);
    	        vfm_map_add(id,  ps);
    	    }
    	    else if (cmd == VFM_CMD_RM){
    	        //printk("vfm_map_remove(%s)\n",id);
    	        if(vfm_map_remove(id)<0){
    	            count = 0;
    	        }
    	    }
    	    break;
	  }
    i++;
	}
	kfree(buf_orig);
	return count;
}

static CLASS_ATTR(map, 0664, vfm_map_show, vfm_map_store);

static struct class vfm_class = {
	.name = CLS_NAME,
};

static int __init vfm_class_init(void)
{
	int error;

	vfm_init();

	error = class_register(&vfm_class);
	if (error) {
		printk(KERN_ERR "%s: class_register failed\n", __func__);
		return error;
	}
	error = class_create_file(&vfm_class, &class_attr_map);
	if (error) {
		printk(KERN_ERR "%s: class_create_file failed\n", __func__);
		class_unregister(&vfm_class);
	}

	return error;

}
static void __exit vfm_class_exit(void)
{
	class_unregister(&vfm_class);
}

fs_initcall(vfm_class_init);
module_exit(vfm_class_exit);

MODULE_PARM_DESC(vfm_debug_flag, "\n vfm_debug_flag \n");
module_param(vfm_debug_flag, int, 0664);

MODULE_PARM_DESC(vfm_map_num, "\n vfm_map_num \n");
module_param(vfm_map_num, int, 0664);

MODULE_DESCRIPTION("Amlogic video frame manager driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bobby Yang <bo.yang@amlogic.com>");


