/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *		this file decode logo data according to it's type.
 *
 *  Author: Amlogic Software
 *  Created: 2010/4/1   19:46
 *
 *******************************************************************/
#include  "logo.h"
 #include	"amlogo_log.h"
#include  <linux/amlogic/amlog.h>
//#define SETUP_SELF_RAISE

static  LIST_HEAD(parser_line);

#ifdef   SETUP_SELF_RAISE 
static  parser_list_t aml_parser[MAX_PIC_TYPE];
#endif

void (*Power_on_bl)(void);
EXPORT_SYMBOL(Power_on_bl);

int  register_logo_parser(logo_parser_t* new_parser)
{

	parser_list_t  *pitem;

#ifdef   SETUP_SELF_RAISE   
	static int count=0;
	pitem=&aml_parser[count++];
#else
	pitem=kmalloc(sizeof(parser_list_t),GFP_KERNEL);
#endif

	pitem->parser=new_parser;
	list_add_tail(&pitem->list,&parser_line);
	return SUCCESS;
}
static int  all_parser_setup(void)
{
	bmp_setup();
	jpeg_setup();
	return SUCCESS;
}
 int start_logo(void)
 {
	parser_list_t  *pitem;
	logo_object_t *plogo=&aml_logo;
	int  found=0;
	int  ret=0 ;
	if(0!=strcmp(plogo->name,LOGO_NAME)){   
	    ret = -LOGO_PARA_UNPARSED;
        goto start_logo_fail;
	} 
	if ((ret=setup_output_device(plogo))!=SUCCESS)//we will use this device to get display info
	{						//for examble: width height 
        goto start_logo_fail;
	}
	if(plogo->para.loaded) //if logo be loaded by uboot or other loader.then return
	return SUCCESS;

	all_parser_setup();	
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"start decode logo\n");	
	list_for_each_entry(pitem,&parser_line,list){
		if(pitem->parser->op.init(plogo)==PARSER_FOUND)	
		{
			found=1;
			amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_HIGH,"parser found,logo type:%s\n",pitem->parser->name);
			break;
		}
	}
	if(0==found){   
	    ret = -ENOPARSER;
        goto start_logo_fail;
	}
	
	if(plogo->parser->op.decode(plogo))
	{
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_HIGH,"decode logo picture fail\n")	;
	    ret = -PARSER_DECODE_FAIL;
        goto start_logo_fail;		
	}
	plogo->dev->op.transfer(plogo);
	plogo->dev->op.deinit();
	plogo->parser->op.deinit(plogo);

	if (Power_on_bl)
		Power_on_bl();
		
	return SUCCESS;	
	
start_logo_fail:
	if (Power_on_bl)
		Power_on_bl();
		
	return ret;	

 }
 int exit_logo(logo_object_t *logo)
 {
 	return 0;
 }
int  unregister_logo_parser(void)
{
	parser_list_t  *pitem,*tmp;
	logo_object_t *plogo=&aml_logo;

	list_for_each_entry_safe(pitem,tmp,&parser_line,list){
		if(pitem)  
		{
			pitem->parser->op.deinit(plogo);
			list_del(&pitem->list );
#ifndef   SETUP_SELF_RAISE 
			kfree(pitem);
#endif			
			
		}
	}
	return SUCCESS;
}
subsys_initcall(start_logo);
