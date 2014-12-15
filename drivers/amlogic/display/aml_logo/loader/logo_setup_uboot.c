/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *		this file will parse all parameters offered by loader.
 *
 *  Author: Amlogic Software
 *  Created: 2010/8/23   19:46
 *
 *******************************************************************/
 #include	"loader.h"
 #include	"amlogo_log.h"
 #include <linux/amlogic/amlog.h>

/******************************************************************
**																   **
**	sample para for bootargs=> logo=osd1,0x84100000,full,720p,dbg,progress  **
**																   **
*******************************************************************/
/******************logo entry point ***************/

MODULE_AMLOG(LOG_LEVEL_MAX-1, LOG_MASK_ALL, LOG_LEVEL_DESC, LOG_MASK_DESC);
logo_object_t  aml_logo={
	.name="default",
	.dev=NULL,
	.parser=NULL,
} ;

static inline  int str2lower(char *str)
{
	while(*str != '\0')
	{
		*str= TOLOWER(*str);
		str++;
	}
	return 0;
}

static inline int install_logo_info(logo_object_t *plogo,char *para)
{
	static  para_info_pair_t para_info_pair[PARA_END+2]={
//head
	{"head",INVALID_INFO,		PARA_END+1,		1,	0,	PARA_END+1},

//dev		
	{"osd0",LOGO_DEV_OSD0,	PARA_FIRST_GROUP_START-1,	PARA_FIRST_GROUP_START+1,	PARA_FIRST_GROUP_START,	PARA_SECOND_GROUP_START-1},
	{"osd1",LOGO_DEV_OSD1,	PARA_FIRST_GROUP_START,		PARA_FIRST_GROUP_START+2,	PARA_FIRST_GROUP_START,	PARA_SECOND_GROUP_START-1},
	{"vid",LOGO_DEV_VID,		PARA_FIRST_GROUP_START+1,	PARA_FIRST_GROUP_START+3,	PARA_FIRST_GROUP_START,	PARA_SECOND_GROUP_START-1},  // 3
	{"mem",LOGO_DEV_MEM,	PARA_FIRST_GROUP_START+2,	PARA_FIRST_GROUP_START+4,	PARA_FIRST_GROUP_START,	PARA_SECOND_GROUP_START-1},
//vmode
	{"480i",VMODE_480I,		PARA_SECOND_GROUP_START-1,	PARA_SECOND_GROUP_START+1,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"480cvbs",VMODE_480CVBS,PARA_SECOND_GROUP_START,	PARA_SECOND_GROUP_START+2,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"480p",VMODE_480P,		PARA_SECOND_GROUP_START+1,	PARA_SECOND_GROUP_START+3,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"576i",VMODE_576I,		PARA_SECOND_GROUP_START+2,	PARA_SECOND_GROUP_START+4,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"576cvbs",VMODE_576CVBS,PARA_SECOND_GROUP_START+3,	PARA_SECOND_GROUP_START+5,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"576p",VMODE_576P,		PARA_SECOND_GROUP_START+4,	PARA_SECOND_GROUP_START+6,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"720p",VMODE_720P,		PARA_SECOND_GROUP_START+5,	PARA_SECOND_GROUP_START+7,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"1080i",VMODE_1080I,		PARA_SECOND_GROUP_START+6,	PARA_SECOND_GROUP_START+8,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"1080p",VMODE_1080P,	PARA_SECOND_GROUP_START+7,	PARA_SECOND_GROUP_START+9,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"panel",VMODE_LCD,			PARA_SECOND_GROUP_START+8,	PARA_SECOND_GROUP_START+10,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"720p50hz",VMODE_720P_50HZ,			PARA_SECOND_GROUP_START+9,	PARA_SECOND_GROUP_START+11,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"1080i50hz",VMODE_1080I_50HZ,			PARA_SECOND_GROUP_START+10,	PARA_SECOND_GROUP_START+12,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"1080p50hz",VMODE_1080P_50HZ,			PARA_SECOND_GROUP_START+11,	PARA_SECOND_GROUP_START+13,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"1080p24hz", VMODE_1080P_24HZ,			PARA_SECOND_GROUP_START+12,	PARA_SECOND_GROUP_START+14, PARA_SECOND_GROUP_START,    PARA_THIRD_GROUP_START-1},
	{"4k2k24hz",VMODE_4K2K_24HZ,			PARA_SECOND_GROUP_START+13,	PARA_SECOND_GROUP_START+15,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"4k2k25hz",VMODE_4K2K_25HZ,			PARA_SECOND_GROUP_START+14,	PARA_SECOND_GROUP_START+16,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"4k2k30hz",VMODE_4K2K_30HZ,			PARA_SECOND_GROUP_START+15,	PARA_SECOND_GROUP_START+17,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"4k2ksmpte",VMODE_4K2K_SMPTE,			PARA_SECOND_GROUP_START+16,	PARA_SECOND_GROUP_START+18,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"lvds1080p",VMODE_LVDS_1080P,			PARA_SECOND_GROUP_START+17,	PARA_SECOND_GROUP_START+19,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
	{"lvds1080p50hz",VMODE_LVDS_1080P_50HZ,			PARA_SECOND_GROUP_START+18,	PARA_SECOND_GROUP_START+20,	PARA_SECOND_GROUP_START,	PARA_THIRD_GROUP_START-1},
//display mode
	{"origin",DISP_MODE_ORIGIN,	PARA_THIRD_GROUP_START-1,	PARA_THIRD_GROUP_START+1,	PARA_THIRD_GROUP_START,PARA_FOURTH_GROUP_START-1},  //15
	{"center",DISP_MODE_CENTER,	PARA_THIRD_GROUP_START,		PARA_THIRD_GROUP_START+2,	PARA_THIRD_GROUP_START,PARA_FOURTH_GROUP_START-1},
	{"full",DISP_MODE_FULL_SCREEN,	PARA_THIRD_GROUP_START+1,	PARA_THIRD_GROUP_START+3,	PARA_THIRD_GROUP_START,PARA_FOURTH_GROUP_START-1},
//dbg
	{"dbg",LOGO_DBG_ENABLE,	PARA_FOURTH_GROUP_START-1,	PARA_FOURTH_GROUP_START+1,	PARA_FOURTH_GROUP_START,PARA_FIFTH_GROUP_START-1},  //18
//progress	
	{"progress",LOGO_PROGRESS_ENABLE,PARA_FIFTH_GROUP_START-1,PARA_FIFTH_GROUP_START+1,PARA_FIFTH_GROUP_START,PARA_SIXTH_GROUP_START-1},
//loaded
	{"loaded",LOGO_LOADED,PARA_SIXTH_GROUP_START-1,PARA_SIXTH_GROUP_START+1,PARA_SIXTH_GROUP_START,PARA_END},

//tail	
	{"tail",INVALID_INFO,PARA_END,0,0,PARA_END+1},
	};

	static u32 tail=PARA_END+1;
	u32 first=para_info_pair[0].next_idx ; 
	u32 i,addr;
	
	for(i=first;i<tail;i=para_info_pair[i].next_idx)
	{
		if(strcmp(para_info_pair[i].name,para)==0)
		{
			u32 group_start=para_info_pair[i].cur_group_start ;
			u32 group_end=para_info_pair[i].cur_group_end;
			u32	prev=para_info_pair[group_start].prev_idx;
			u32  next=para_info_pair[group_end].next_idx;
			amlog_level(LOG_LEVEL_MAX,"%s:%d\n",para_info_pair[i].name,para_info_pair[i].info);
			switch(para_info_pair[i].cur_group_start)
			{
				case PARA_FIRST_GROUP_START:
				plogo->para.output_dev_type=(platform_dev_t)para_info_pair[i].info;
				break;
				case PARA_SECOND_GROUP_START:
				plogo->para.vout_mode=(vmode_t)para_info_pair[i].info;
				break;
				case PARA_THIRD_GROUP_START:
				plogo->para.dis_mode=(logo_display_mode_t)para_info_pair[i].info;
				break;
				case PARA_FOURTH_GROUP_START:
				amlog_level(LOG_LEVEL_MAX,"select debug mode\n");	
				amlog_level_logo=AMLOG_DEFAULT_LEVEL;
				amlog_mask_logo=AMLOG_DEFAULT_MASK;
				break;
				case PARA_FIFTH_GROUP_START:
				plogo->para.progress=1;
				break;	
				case PARA_SIXTH_GROUP_START:
				plogo->para.loaded=1;
				amlog_level(LOG_LEVEL_MAX,"logo has been loaded\n");
				break;	
			}
			para_info_pair[prev].next_idx=next;
			para_info_pair[next].prev_idx=prev;
			return 0;
		}//addr we will deal with it specially. 
	}
	addr=simple_strtoul(para, NULL,16);
	//addr we will deal with it specially. 
	if(addr >=PHYS_OFFSET)
	{
		plogo->para.mem_addr=(char*)phys_to_virt(addr);
		amlog_mask_level(LOG_MASK_LOADER,LOG_LEVEL_LOW,"mem_addr:0x%p\n",plogo->para.mem_addr);
	}
	return 0;
}
logo_object_t*	 get_current_logo_obj(void)
{
	if((aml_logo.dev ==NULL || aml_logo.parser ==NULL)&&!aml_logo.para.loaded)
	{
		return NULL;
	}
	if ( aml_logo.dev != NULL &&  aml_logo.dev->output_dev.osd.color_depth == 0)
        aml_logo.dev->output_dev.osd.color_depth=32;
	return &aml_logo;
}

vmode_t get_resolution_vmode(void)
{
	logo_object_t *plogo=&aml_logo;

	if (plogo != NULL)
		return plogo->para.vout_mode;
	else
		return VMODE_1080P;
}

vmode_set_t get_current_mode_state(void)
{
	if(aml_logo.dev == NULL)
	{
		return VMODE_NOT_SETTED;
	}else{
		return VMODE_SETTED;
	}
}

int  __init  logo_setup(char *str)
{

	char	     *ptr=str;
	char 	       sep[2];
	char      *option;
	int     	count=6;
	char	   	find=0;
	logo_object_t	*plogo;


	if(NULL==str)
	{
		return -EINVAL;
	}
	plogo=&aml_logo;
	memset(plogo,0,sizeof(logo_object_t));
	sprintf(plogo->name,LOGO_NAME);
	
	do
	{
		if(!isalpha(*ptr)&&!isdigit(*ptr))
		{
			find=1;
			break;
		}
	}while(*++ptr != '\0');
	if(!find) return -EINVAL;
	sep[0]=*ptr;
	sep[1]='\0' ;
	while((count--) && (option=strsep(&str,sep)))
	{
		str2lower(option);
		install_logo_info(plogo,option); //all has been parsed.
	}
	return 0;
}


__setup("logo=",logo_setup) ;
