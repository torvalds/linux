/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *		this file decode logo data according to it's type.
 *
 *  Author: Amlogic Software
 *  Created: 
 *
 *******************************************************************/
#include  "logo.h"
#include	"amlogo_log.h"
#include <linux/amlogic/amlog.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
static  LIST_HEAD(output_dev_line);
static  output_dev_list_t aml_output_dev[LOGO_DEV_MAX];

 int match_device_name(struct device *dev, void *data)
{
#define DEVICE_NAME_LEN		7
	const char *name = data;

	if (strncmp(name, dev_name(dev),DEVICE_NAME_LEN) == 0)
		return 1;
	return 0;
}
 static struct resource memobj;
 int   setup_logo_platform_resource(logo_object_t *logo)
{
	int  i;
	const char  *device_name[3]={"mesonfb0","mesonfb1","amstream"};
	struct  device  *dev=NULL;
	struct platform_device  * platform_dev;
	struct resource * res; 
	int idx;

	for(i=0;i<LOGO_DEV_MEM;i++)
	{
		 u32  num=i%2;
		 strcpy(logo->platform_res[i].name,device_name[i]);
		 amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"device name:%s\n",logo->platform_res[i].name);
		 if(1 != i) //osd1 special one
		 dev=bus_find_device(&platform_bus_type, NULL, logo->platform_res[i].name, match_device_name) ;
		 if(dev)
		{
			
			platform_dev =dev_to_platformdev(dev) ;
			amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"got platform resource\n");
#if 0			
			res=platform_get_resource(platform_dev,IORESOURCE_MEM,num);//something special.
#else
			res = &memobj;
			idx = find_reserve_block(platform_dev->dev.of_node->name,num);
			if(idx < 0){
				amlog_mask_level(LOG_MASK_DEVICE, LOG_LEVEL_HIGH,"can not find %s %d reserve block\n",platform_dev->dev.of_node->name, num);
				continue;
			}
			res->start = (phys_addr_t)get_reserve_block_addr(idx);
			res->end = res->start+ (phys_addr_t)get_reserve_block_size(idx)-1;
#endif
			if(res)
			{
				amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"resource: start=0x%x,end=0x%x\r\n",res->start,res->end);
				logo->platform_res[i].mem_start=res->start;
				logo->platform_res[i].mem_end=res->end;
			}else{
				amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_HIGH,"can't get device resource\n");
			}
			
		}
	}
	return SUCCESS;
}
int  register_logo_output_dev(logo_output_dev_t* new_dev)
{
	output_dev_list_t  *pitem;
	static int count=0;
	
	pitem=&aml_output_dev[count++];
	pitem->dev=new_dev;
	list_add_tail(&pitem->list,&output_dev_line);
	return SUCCESS;
}
static int  load_all_output_dev(void)
{
	dev_osd_setup();
	dev_vid_setup();
	return SUCCESS;
}
//entry point for this file .
int  setup_output_device(logo_object_t *plogo)
{
	output_dev_list_t  *pitem;
	int  found=0;
	if(setup_logo_platform_resource(plogo))
	{
		return -ENOSPC;
	}
	load_all_output_dev();
	list_for_each_entry(pitem,&output_dev_line,list){
		if(pitem->dev->op.init(plogo)==OUTPUT_DEV_FOUND)	
		{
			found=1;
			amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"output device setup OK,logo dev index:0x%d\n",plogo->dev->idx);
			return  SUCCESS; 
		}
	}
	return -OUTPUT_DEV_SETUP_FAIL;
}
 
int  unregister_logo_output_dev(void)
{
	output_dev_list_t  *pitem,*tmp;

	list_for_each_entry_safe(pitem,tmp,&output_dev_line,list){
		if(pitem)  
		{
			pitem->dev->op.deinit();
			list_del(&pitem->list );
			kfree(pitem);
		}
	}
	return SUCCESS;
}
