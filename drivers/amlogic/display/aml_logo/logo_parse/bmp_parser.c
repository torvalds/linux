/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *		parse bmp data 
 *
 *  Author: Amlogic Software
 *  Created: 2010/4/1   19:46
 *
 *******************************************************************/
#include "logo.h"
#include "bmp_parser.h"
#include <asm/cacheflush.h>
 #include	"amlogo_log.h"
#include <linux/amlogic/amlog.h>

static  logo_parser_t    logo_bmp_parser={
 	.name="bmp",
	.op={
	 .init = bmp_init,
	 .decode=bmp_decode,
	 .deinit=bmp_deinit,
	}
 };

/**************************************************************************
**	before we setup parser output addr ,output device info and ************************
**	pic info has been setup already.						   **********************
** 	different  pic type will select different output place      	 **************************                                                                 
**************************************************************************/
static  int  setup_parser_output_addr(logo_object_t *plogo)
{
	int  screen_mem_start;
	int  screen_size ;

	if(plogo->para.output_dev_type > LOGO_DEV_OSD1) //bmp pic decoded into video layer 
	{											//not supported .
		return -1;
	}
	screen_mem_start=plogo->platform_res[plogo->para.output_dev_type].mem_start;
	screen_size=plogo->dev->vinfo->width*plogo->dev->vinfo->height*(plogo->parser->decoder.bmp.color_depth>>3);
	//double buffer ,bottom part .
	plogo->parser->output_addr=(char *)phys_to_virt(screen_mem_start + screen_size);
	plogo->need_transfer=TRUE;
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"bmp decode output addr:0x%p,%s\n",plogo->parser->output_addr,plogo->need_transfer?"transfer":"no transfer");
	return 0;
}
/****************************************************************************/
/*****************************************************************************/
static  int  bmp_decode(logo_object_t *plogo)
{
	char	*in=plogo->para.mem_addr ;
	char *out=plogo->parser->output_addr;
	char *bmp_data;
	int 	i;
	bmp_header_t  *bmp_header=(bmp_header_t  *)plogo->parser->priv;
	int bpp=plogo->parser->logo_pic_info.color_info/8 ;
	int width=plogo->parser->logo_pic_info.width;
	int height=plogo->parser->logo_pic_info.height;
	int line_length=width*bpp;
	int bmp_size=line_length*height;

	if(NULL==in || NULL==out)  return FAIL;
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"in addr:0x%p  out addr:0x%p,bmp size :%d,osd width:%d,h=%d, bmp_width:%d, height=%d.\n",in,out,bmp_size,plogo->dev->vinfo->width,plogo->dev->vinfo->height, width, height);
	//decode data to out buffer
	bmp_data=(char*)(in+bmp_header->bmp_file_header->bfOffBits+line_length*(height-1));
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"data offset:%d\n",bmp_header->bmp_file_header->bfOffBits);
	for (i=0;i<height ;i++)
	{
		memcpy(out,bmp_data,line_length);
		out+=width*bpp;
		bmp_data-=line_length;
	}
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"get bmp data completed\n");
	return SUCCESS;
}
static int bmp_init(logo_object_t *logo)
{
	BITMAPFILEHEADER *header;
	BITMAPINFOHEADER *bmp_info_header;
	bmp_header_t		*bmp_header;
	void  __iomem*	logo_vaddr=logo->para.mem_addr;

	header=(BITMAPFILEHEADER*)logo_vaddr;
	bmp_info_header=(BITMAPINFOHEADER*)(logo_vaddr+sizeof(BITMAPFILEHEADER));
	
	
	if (NULL==header) goto error; 
	if(header->bfType == 0x4d42) //"BM"
	{
		logo->parser=&logo_bmp_parser;
		logo->parser->decoder.bmp.color_depth=bmp_info_header->biBitCount;
		logo->parser->logo_pic_info.color_info=logo->parser->decoder.bmp.color_depth;
		logo->parser->logo_pic_info.width=bmp_info_header->biWidth;
		logo->parser->logo_pic_info.height=bmp_info_header->biHeight;
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"bmp color depth:%d\n",logo->parser->decoder.bmp.color_depth);
		if(bmp_info_header->biBitCount<16)
		{
			amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_HIGH,"color depth less than 16 not supported\n");
			goto error;
		}
		if(!setup_parser_output_addr(logo))
		{
			bmp_header=kmalloc(sizeof(bmp_header_t),GFP_KERNEL);
			bmp_header->bmp_file_header=header;
			bmp_header->bmp_info_header=bmp_info_header;
			logo->parser->priv=bmp_header ;
			logo->para.mem_addr=(char *)logo_vaddr;
			return PARSER_FOUND;
		}
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_HIGH,"bmp can only display on osd0 or osd1 ,other layer not supported\n");
	}
error:
	return PARSER_UNFOUND;
	
}
static int  bmp_deinit(logo_object_t *plogo)
{
	bmp_header_t	*bmp_header=(bmp_header_t*)plogo->parser->priv;
	
	if(bmp_header)
	{
		kfree(bmp_header);
	}
	return SUCCESS;
}


int bmp_setup(void)
{
	register_logo_parser(&logo_bmp_parser);
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"bmp setup\n");
	return SUCCESS;
}
	
//arch_initcall(bmp_setup) ;
