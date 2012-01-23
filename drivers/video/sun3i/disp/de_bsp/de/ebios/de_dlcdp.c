/*
*******************************************************************************************************************
*                                                         	display driver
*                                         			the display dirver support module
*
*                             				 Copyright(C), 2006-2008, SoftWinners Microelectronic Co., Ltd.
*											               All Rights Reserved
*
*File Name£º    de_dlcdp.c
*
*Author£º       William Wang
*
*Version :      1.1.0
*
*Date:          2008-6-6
*
*Description :  display engine direct lcd pipe bsp interface implement
*
*Others :       None at present.
*
* History :
*
* <Author>          <time>      <version>     <description>
*
* William Wang     2008-6-6         1.1.0          Create File
*
*******************************************************************************************************************
*/
#include "de_bsp_i.h"

//==================================================================
//function name£º   Graphic_Format_Get_Bpp
//author£º
//date£º            2008-5-4
//description£º     get framebuffer address offset framebuffer address
//parameters£º      hlayer       the layer attribute need to request
//return£º          success returns framebuffer offset value
//                  fail  returns the number of failed
//modify history£º
//==================================================================
static __u8  DLcdP_Format_Get_Bpp(__u8 format)
{
    __u8 bpp;

    switch(format)
    {
        case  DE_IF1BPP:      /*internal framebuffer internal framebuffer 1bpp */
			bpp = 1;
			break;

        case DE_IF2BPP:      /*internal framebuffer data 2bpp */
			bpp = 2;
			break;

        case DE_IF4BPP:      /*internal framebuffer 4bpp */
			bpp = 4;
			break;

        case DE_IF8BPP:      /*internal framebuffer 8bpp */
			bpp = 8;
			break;

		default:
            bpp = 0;
			break;
     }

   return bpp;
  }



//==================================================================
//function name:    DE_BE_Set_DLCDP_Start_Switch
//author:
//date:             2008-6-3
//description:      de be set dlcdp start switch
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

static __s32 DE_BE_Set_DLCDP_Start_Switch(__bool rst_start)
{
    __u8 value;
    value = DE_BE_RUINT8IDX(DE_BE_MODE_CTL_OFF,0);
    if(rst_start)
      DE_BE_WUINT8IDX(DE_BE_MODE_CTL_OFF,0,value|0x04);
    else
      DE_BE_WUINT8IDX(DE_BE_MODE_CTL_OFF,0,value&(~0x04));
    return 0;
}

//==================================================================
//function name:    DE_BE_Set_DLcdP_WfbLine
//author:
//date:             2008-6-3
//description:      de be set dlcdp wfbline
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

static __s32 DE_BE_Set_DLcdP_WfbLine(__u32 width)
{
   __u32 readval;
    readval=DE_BE_RUINT32(DE_BE_DLCDP_FRMBUF_ADDRCTL_OFF);
    DE_BE_WUINT32(DE_BE_DLCDP_FRMBUF_ADDRCTL_OFF,(readval&0xffff0000)|width);
    return 0;
}


//==================================================================
//function name:    DE_BE_Set_DLcdP_FrmBufAddr
//author:
//date:             2008-6-3
//description:      de be set dlcdp frmbufaddr
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

static __s32 DE_BE_Set_DLcdP_FrmBufAddr(__u32 addr)
{
    __u32 readval;

    readval = DE_BE_RUINT32(DE_BE_DLCDP_FRMBUF_ADDRCTL_OFF);
    DE_BE_WUINT32(DE_BE_DLCDP_FRMBUF_ADDRCTL_OFF,(readval & 0xffff) | addr<<16);

    return 0;
}

//==================================================================
//function name:    DE_BE_Set_DLcdP_FrmBufFmt
//author:
//date:             2008-6-3
//description:      de be set dlcdp frmbuffmt
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

static __s32 DE_BE_Set_DLcdP_FrmBufFmt(__u8 fmt,__u8 order)
{
   __u32 readval;
    readval=DE_BE_RUINT32(DE_BE_DLCDP_CTL_OFF);
    DE_BE_WUINT32(DE_BE_DLCDP_CTL_OFF,(readval&0xf00ff)|fmt<<10|order<<8);
   return 0;
}

//==================================================================
//function name:    DE_BE_Set_DLcdP_Enable
//author:
//date:             2008-6-3
//description:      de be set dlcdp enable
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

__s32 DE_BE_DLcdP_Enable(void)
{
    __u32 readval;

    DE_BE_Set_DLCDP_Start_Switch(1);
    readval = DE_BE_RUINT8(DE_BE_DLCDP_CTL_OFF);
    DE_BE_WUINT8(DE_BE_DLCDP_CTL_OFF,(readval & 0xffffe) | 0x01);

  return 0;
}

//==================================================================
//function name:    DE_BE_Set_DLcdP_Enable
//author:
//date:             2008-6-3
//description:      de be set dlcdp enable
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

__s32 DE_BE_DLcdP_Disable(void)
{
     __u32 readval;

     readval=DE_BE_RUINT8(DE_BE_DLCDP_CTL_OFF);
     DE_BE_WUINT8(DE_BE_DLCDP_CTL_OFF,(readval&0xffffe));
     DE_BE_Set_DLCDP_Start_Switch(0);

     return 0;
}

//==================================================================
//function name:    DE_BE_DLcdP_Set_Factor
//author:
//date:             2008-6-3
//description:      de be set dlcdp factor
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

__s32 DE_BE_DLcdP_Set_Factor(__u8 hfactor, __u8 vfactor)
{
   __u32 readval;

    readval = DE_BE_RUINT32(DE_BE_DLCDP_CTL_OFF);
    DE_BE_WUINT32(DE_BE_DLCDP_CTL_OFF,(readval&0xffff)|(hfactor>>1)<<18|(vfactor>>1)<<16);
    return 0;
}


//==================================================================
//function name:    DE_BE_Set_DLcdP_Size
//author:
//date:             2008-6-3
//description:      de be set dlcdp size
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

__s32 DE_BE_DLcdP_Set_Regn(__disp_rect_t *rect)
{
   __u32 readval0,readval1;

   readval0 = DE_BE_RUINT32(DE_BE_DLCDP_CRD_CTL_OFF0);
   readval1 = DE_BE_RUINT32(DE_BE_DLCDP_CRD_CTL_OFF1);
   DE_BE_WUINT32(DE_BE_DLCDP_CRD_CTL_OFF0,(readval0 & 0x0) | (rect->y<<16) | rect->x);
   DE_BE_WUINT32(DE_BE_DLCDP_CRD_CTL_OFF1,(readval1 & 0x0)|((rect->height+rect->y)<<16)|(rect->width+rect->x));

   return 0;
}

//==================================================================
//function name:    DE_BE_Get_DLcdP_Size
//author:
//date:             2008-6-3
//description:      de be get dlcdp size
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

__s32 DE_BE_DLcdP_Get_Regn(__disp_rect_t *rect)
{
   __u32 readval0,readval1;

   readval0 = DE_BE_RUINT32(DE_BE_DLCDP_CRD_CTL_OFF0);
   readval1 = DE_BE_RUINT32(DE_BE_DLCDP_CRD_CTL_OFF1);

   rect->x = readval0 & 0xffff;
   rect->y = (readval0 & 0xffff)>>16;
   rect->width = (readval1 & 0xffff) - rect->x;
   rect->height = ((readval1& 0xffff0000)>>16) - rect->y;

   return 0;
}

//==================================================================
//function name:    DE_BE_DLcdP_Set_FrameBuffer
//author:
//date:             2008-6-7
//description:      de be set dlcdp framebuffer
//parameters:
//return:           if success return DIS_SUCCESS
//                  if fail return the number of fail
//modify history:
//==================================================================

__s32 DE_BE_DLcdP_Set_FrameBuffer(de_dlcdp_src_t *layer_fb)
{
	__u8  bpp;
	__u32 addr;

	bpp = DLcdP_Format_Get_Bpp(layer_fb->format);/*get layer framebuffer format bpp */
	if(bpp <= 0)
	{
		return -1;
	}
	addr = DE_BE_Offset_To_Addr(layer_fb->fb_addr, layer_fb->fb_width, layer_fb->offset_x, layer_fb->offset_y,bpp);

	DE_BE_Set_Internal_Framebuffer(addr,DE_BE_INTERNAL_FB_SIZE); /*set framebuffer data to internal framebuffer  */
	DE_BE_Set_DLcdP_FrmBufAddr(0);   /*set internal framebuffer address to 0 */
	DE_BE_Set_DLcdP_WfbLine(bpp*(layer_fb->fb_width));
	DE_BE_Set_DLcdP_FrmBufFmt(layer_fb->format,layer_fb->pixseq);

	return 0;
}

