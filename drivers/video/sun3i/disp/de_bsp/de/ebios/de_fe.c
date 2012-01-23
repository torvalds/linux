//*****************************************************************************
//  All Winner Micro, All Right Reserved. 2006-2010 Copyright (c)
//
//  File name   :        de_scal_bsp.c
//
//  Description :  display engine scaler base functions implement for aw1620
//
//  History       :
//                2010/11/09      zchmin       v0.1    Initial version
//******************************************************************************


#include "de_fe.h"

static volatile __de_scal_dev_t * scal_dev[2];
static __u32 de_scal_ch0_offset;
static __u32 de_scal_ch1_offset;
static __u32 de_scal_ch2_offset;


//*********************************************************************************************
// function          : iDE_SCAL_Set_Tape_Offset(__u8 sel, __u8 en, __u8 ch, __u8 dir__s32 factor)
// description      : set scaler tape offset
// parameters     :
//                 sel <scaler select>
//                 en <0, 1>
//                 |    0   disable
//                 |    1   enable
//                 ch <0,1>
//                 |    0   channel 0
//                 |    1   channel 12
//                 dir <0,1>
//                 |    0   horizontal
//                 |    1   vertical
//                 factor <scaling factor, 16 bit  fraction>
// return            :
//               success
//***********************************************************************************************
static __s32 iDE_SCAL_Set_Tape_Offset(__u8 sel, __u8 en, __u8 ch, __u8 dir, __s32 factor)
{
    __s32 stepw;
    __s8 tape0, tape1, tape2, tape3;
	   return 0;	//modify zchmin 110317

    stepw = factor>>16;


    if(sel == 1)   //only scaler 0 support tape offset setting
    {
        return 0;
    }
    if(en) //open
    {
        if(stepw>0x3f)
        {
            stepw = 59;
        }
        else if(stepw>4)
        {
            stepw -= 3;
        }
        else
        {
            stepw = 0x0;
        }
    }
    else
    {
        stepw = 0x0;
    }
    tape0 = -1 - (stepw>>1);
    tape1 = 0;
    tape2 = 1 + (stepw>>2);
    tape3 = 2 + ((stepw + 1)>>1);
    if(tape1 == tape2)
    {
        tape2 += 1;
    }
    if(tape2 == tape3)
    {
        tape3 += 1;
    }
    if((ch==0) && (dir==0))
    {
        scal_dev[sel]->ch0_h_tape_offset.tape0 = tape0&0x7f;
        scal_dev[sel]->ch0_h_tape_offset.tape1 = (tape1-tape0)&0x7f;
        scal_dev[sel]->ch0_h_tape_offset.tape2 = (tape2-tape1)&0x7f;
        scal_dev[sel]->ch0_h_tape_offset.tape3 = (tape3-tape2)&0x7f;
    }
    else if((ch==0) && (dir==1))
    {
        scal_dev[sel]->ch0_v_tape_offset.tape0 = tape0&0x7f;
        scal_dev[sel]->ch0_v_tape_offset.tape1 = (tape1-tape0)&0x7f;
        scal_dev[sel]->ch0_v_tape_offset.tape2 = (tape2-tape1)&0x7f;
        scal_dev[sel]->ch0_v_tape_offset.tape3 = (tape3-tape2)&0x7f;
    }
    else if((ch==1) && (dir==0))
    {
        scal_dev[sel]->ch12_h_tape_offset.tape0 = tape0&0x7f;
        scal_dev[sel]->ch12_h_tape_offset.tape1 = (tape1-tape0)&0x7f;
        scal_dev[sel]->ch12_h_tape_offset.tape2 = (tape2-tape1)&0x7f;
        scal_dev[sel]->ch12_h_tape_offset.tape3 = (tape3-tape2)&0x7f;
    }
    else if((ch==1) && (dir==1))
    {
        scal_dev[sel]->ch12_v_tape_offset.tape0 = tape0&0x7f;
        scal_dev[sel]->ch12_v_tape_offset.tape1 = (tape1-tape0)&0x7f;
        scal_dev[sel]->ch12_v_tape_offset.tape2 = (tape2-tape1)&0x7f;
        scal_dev[sel]->ch12_v_tape_offset.tape3 = (tape3-tape2)&0x7f;
    }

    return 0;
}

//*********************************************************************************************
// function          : DE_SCAL_Set_Reg_Base(__u8 sel, __u32 base)
// description      : set scale reg base
// parameters     :
//                 sel <scaler select>
//                 base  <reg base>
// return              :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Reg_Base(__u8 sel, __u32 base)
{
	scal_dev[sel]=(__de_scal_dev_t *)base;

	return 0;
}

//*********************************************************************************************
// function         : DE_SCAL_Get_Reg_Base(__u8 sel)
// description     : get scale reg base
// parameters    :
//                 sel <scaler select>
// return            :
//               reg base
//***********************************************************************************************
__u32 DE_SCAL_Get_Reg_Base(__u8 sel)
{
	__u32 ret = 0;

	ret = (__u32)(scal_dev[sel]);

	return ret;
}

//*********************************************************************************************
// function         : DE_SCAL_Config_Src(__u8 sel, __scal_buf_addr_t *addr, __scal_src_size_t *size,
//                                       __scal_src_type_t *type, __u8 field, __u8 dien)
// description     : scaler source concerning parameter configure
// parameters    :
//                 sel <scaler select>
//                 addr  <frame buffer address for 3 channel, 32 bit absolute address>
//                 size <scale region define,  src size, offset, scal size>
//                 type <src data type, include byte sequence, mode, format, pixel sequence>
//                 field <frame/field data get>
//                 dien <deinterlace enable>
// return            :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Config_Src(__u8 sel, __scal_buf_addr_t *addr, __scal_src_size_t *size,
                         __scal_src_type_t *type, __u8 field, __u8 dien)

{
    __u8 w_shift, h_shift;
	__u32 image_w0, image_w1;
	__u32 x_off0, y_off0, x_off1, y_off1;
	__u32 in_w0, in_h0, in_w1, in_h1;

	image_w0 = size->src_width;
	in_w0 = size->scal_width;
	in_h0 = size->scal_height;
	x_off0 = size->x_off;
	y_off0 = (field | dien) ? (size->y_off & 0xfffffffe) : size->y_off;  //scan mod enable or deinterlace, odd dy un-support

    if(sel == 0)   //scaler 0
    {
        if(type->fmt == DE_SCAL_INYUV422 || type->fmt == DE_SCAL_INYUV420)
        {
            w_shift = 1;
        	image_w1 = (image_w0 + 0x1)>>w_shift;
        	in_w1 = (in_w0 + 0x1)>>w_shift;
        	x_off1 = (x_off0)>>w_shift;
        	if(type->mod == DE_SCAL_INTER_LEAVED)
        	{
        	    image_w0 = (image_w0 + 0x1) & 0xfffffffe;
                image_w1 = image_w0>>1;
                in_w0 &= 0xfffffffe;
                in_w1 = in_w0>>0x1;
        	    x_off0 &= 0xfffffffe;
        		x_off1 = x_off0>>w_shift;
        	}
        }
        else if(type->fmt == DE_SCAL_INYUV411)
        {
            w_shift = 2;
        	image_w1 = (image_w0 + 0x3)>>w_shift;
        	in_w1 = (in_w0 + 0x3)>>w_shift;
        	x_off1 = (x_off0)>>w_shift;
        }
        else
        {
            w_shift = 0;
        	image_w1 = image_w0;
        	in_w1 = in_w0;
        	x_off1 = x_off0;
        }
        if(type->fmt == DE_SCAL_INYUV420 || type->fmt == DE_SCAL_INCSIRGB)
        {
            h_shift = 1;
        	in_h1 = (in_h0 + 0x1)>>h_shift;
        	y_off1 = (y_off0)>>h_shift;
        }
        else
        {
            h_shift = 0;
        	in_h1 = in_h0;
        	y_off1 = y_off0;
        }
    }
    else   //sel == 1, scaler 1,
    {
        if(type->fmt == DE_SCAL_INYUV422 || type->fmt == DE_SCAL_INYUV420)
        {
            w_shift = 1;
            image_w0 = (image_w0 + 1)&0xfffffffe;
        	image_w1 = (image_w0)>>w_shift;
            in_w0 = in_w0 & 0xfffffffe;
        	in_w1 = (in_w0 + 0x1)>>w_shift;
            x_off0 = x_off0 & 0xfffffffe;
        	x_off1 = (x_off0)>>w_shift;

        }
        else if(type->fmt == DE_SCAL_INYUV411)
        {
            w_shift = 2;
        	image_w1 = (image_w0 + 0x3)>>w_shift;
            in_w0 &= 0xfffffffc;
        	in_w1 = (in_w0 + 0x3)>>w_shift;
            x_off0 &= 0xfffffffc;
        	x_off1 = (x_off0)>>w_shift;
        }
        else
        {
            w_shift = 0;
        	image_w1 = image_w0;
        	in_w1 = in_w0;
        	x_off1 = x_off0;
        }
        if(type->fmt == DE_SCAL_INYUV420 || type->fmt == DE_SCAL_INCSIRGB)
        {
            h_shift = 1;
            in_h0 &= 0xfffffffe;
        	in_h1 = (in_h0 + 0x1)>>h_shift;
            y_off0 &= 0xfffffffe;
        	y_off1 = (y_off0)>>h_shift;
        }
        else
        {
            h_shift = 0;
        	in_h1 = in_h0;
        	y_off1 = y_off0;
        }
    }

	if(type->mod == DE_SCAL_PLANNAR)
	{
	    scal_dev[sel]->stride[0] = image_w0;
		scal_dev[sel]->stride[1] = image_w1;
		scal_dev[sel]->stride[2] = image_w1;

        de_scal_ch0_offset = image_w0 * y_off0 + x_off0;
        de_scal_ch1_offset = image_w1 * y_off1 + x_off1;
        de_scal_ch2_offset = image_w1 * y_off1 + x_off1;
		scal_dev[sel]->buf_addr[0] = addr->ch0_addr+ de_scal_ch0_offset;
		scal_dev[sel]->buf_addr[1] = addr->ch1_addr+ de_scal_ch1_offset;
		scal_dev[sel]->buf_addr[2] = addr->ch2_addr+ de_scal_ch2_offset;
	}
	else if(type->mod == DE_SCAL_INTER_LEAVED)
	{
	    scal_dev[sel]->stride[0] = image_w0<<(0x2 - w_shift);
		scal_dev[sel]->stride[1] = 0x0;
		scal_dev[sel]->stride[2] = 0x0;

        de_scal_ch0_offset = ((image_w0 * y_off0 + x_off0)<<(0x2 - w_shift));
        de_scal_ch1_offset = 0x0;
        de_scal_ch2_offset = 0x0;
		scal_dev[sel]->buf_addr[0] = addr->ch0_addr+ de_scal_ch0_offset;
		scal_dev[sel]->buf_addr[1] = addr->ch1_addr;
		scal_dev[sel]->buf_addr[2] = addr->ch2_addr;
	}
	else if(type->mod == DE_SCAL_UVCOMBINED)
	{
	    scal_dev[sel]->stride[0] = image_w0;
		scal_dev[sel]->stride[1] = image_w1<<1;
		scal_dev[sel]->stride[2] = 0x0;

        de_scal_ch0_offset = image_w0 * y_off0 + x_off0;
        de_scal_ch1_offset = (((image_w1) * (y_off1) + (x_off1))<<1);
        de_scal_ch2_offset = 0x0;
		scal_dev[sel]->buf_addr[0] = addr->ch0_addr+ de_scal_ch0_offset;
		scal_dev[sel]->buf_addr[1] = addr->ch1_addr+ de_scal_ch1_offset;
		scal_dev[sel]->buf_addr[2] = addr->ch2_addr;
	}
	else if(type->mod == DE_SCAL_PLANNARMB)
	{
	    image_w0 = (image_w0 + 0xf)&0xfff0;
        image_w1 = (image_w1 + (0xf>>w_shift)) & (~(0xf>>w_shift));

        //block offset
        scal_dev[sel]->mb_off[0].xoffset0 = (x_off0 & 0x0f);
        scal_dev[sel]->mb_off[0].yoffset0 = (y_off0 & 0x0f);
        scal_dev[sel]->mb_off[0].xoffset1 = (((x_off0 & 0x0f) & (0x0f)) + in_w0 + 0x0f) & 0x0f;
        scal_dev[sel]->mb_off[1].xoffset0 = ((x_off1)&(0x0f>>w_shift));
        scal_dev[sel]->mb_off[1].yoffset0 = ((y_off1)&(0x0f>>h_shift));
        scal_dev[sel]->mb_off[1].xoffset1 = ((((x_off1)&(0x0f>>w_shift)) & (0x0f>>w_shift)) + (in_w1) + (0x0f>>w_shift))&(0x0f>>w_shift);
		scal_dev[sel]->mb_off[2].xoffset0 = scal_dev[sel]->mb_off[1].xoffset0;
		scal_dev[sel]->mb_off[2].yoffset0 = scal_dev[sel]->mb_off[1].yoffset0;
		scal_dev[sel]->mb_off[2].xoffset1 = scal_dev[sel]->mb_off[1].xoffset1;

		scal_dev[sel]->stride[0] = (image_w0 - 0xf)<<4;
		scal_dev[sel]->stride[1] = ((image_w1) <<(0x04-h_shift)) - ((0xf>>h_shift)<<(0x04-w_shift));
		scal_dev[sel]->stride[2] = scal_dev[sel]->stride[1];

        de_scal_ch0_offset = ((image_w0 + 0x0f)&0xfff0) * (y_off0&0xfff0) + ((y_off0&0x00f)<<4) + ((x_off0&0xff0)<<4);
        de_scal_ch1_offset = (((image_w1) + (0x0f>>w_shift)) &(0xfff0>>w_shift)) * ((y_off1) & (0xfff0>>h_shift)) +
                             ((((y_off1) & (0x00f>>h_shift))<<(0x4 - w_shift))) + (((x_off1) & (0xfff0>>w_shift))<<(0x4 - h_shift));
        de_scal_ch2_offset = de_scal_ch1_offset;
		scal_dev[sel]->buf_addr[0] = addr->ch0_addr+ de_scal_ch0_offset;
		scal_dev[sel]->buf_addr[1] = addr->ch1_addr+ de_scal_ch1_offset;
		scal_dev[sel]->buf_addr[2] = addr->ch2_addr+ de_scal_ch2_offset;
	}
	else if(type->mod == DE_SCAL_UVCOMBINEDMB)
	{
	    image_w0 = (image_w0 + 0x1f)&0xffffffe0;
		image_w1 = (image_w1 + 0x0f)&0xfffffff0;
		//block offset
		scal_dev[sel]->mb_off[0].xoffset0 = (x_off0 & 0x1f);
        scal_dev[sel]->mb_off[0].yoffset0 = (y_off0 & 0x1f);
		scal_dev[sel]->mb_off[0].xoffset1 = (((x_off0 & 0x1f) & 0x1f) + in_w0 + 0x1f) &0x1f;
		scal_dev[sel]->mb_off[1].xoffset0 = (((x_off1)<<1)&0x1f);
        scal_dev[sel]->mb_off[1].yoffset0 = ((y_off1)&0x1f);
        scal_dev[sel]->mb_off[1].xoffset1 = (((((x_off1)<<1)&0x1f) & 0x1e) + ((in_w1)<<1) + 0x1f) & 0x1f;

		scal_dev[sel]->stride[0] = (((image_w0 + 0x1f)&0xffe0) - 0x1f)<<0x05;
        scal_dev[sel]->stride[1] = (((((image_w1)<<1)+0x1f)&0xffe0) - 0x1f) << 0x05;
        scal_dev[sel]->stride[2] = 0x00;

        de_scal_ch0_offset = ((image_w0 + 0x1f) &0xffe0) * (y_off0& 0xffe0) + ((y_off0& 0x01f)<<5) + ((x_off0& 0xffe0)<<5);
        de_scal_ch1_offset = (((image_w1<< 0x01)+0x1f)&0xffe0) * ((y_off1) & 0xffe0) + (((y_off1) & 0x01f)<<5) + (((x_off1<<0x01) & 0xffe0)<<5);
        de_scal_ch2_offset = 0x0;
		scal_dev[sel]->buf_addr[0] = addr->ch0_addr+ de_scal_ch0_offset;
        scal_dev[sel]->buf_addr[1] = addr->ch1_addr+ de_scal_ch1_offset;
        scal_dev[sel]->buf_addr[2] = 0x0;
	}

	scal_dev[sel]->input_fmt.byte_seq = type->byte_seq;
	scal_dev[sel]->input_fmt.data_mod = type->mod;
	scal_dev[sel]->input_fmt.data_fmt = type->fmt;
	scal_dev[sel]->input_fmt.data_ps = type->ps;

	scal_dev[sel]->ch0_in_size.width = in_w0 - 1;
	scal_dev[sel]->ch0_in_size.height = in_h0 - 1;
	scal_dev[sel]->ch12_in_size.width = in_w1 - 1;
	scal_dev[sel]->ch12_in_size.height = in_h1 - 1;


    return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_Fb_Addr(__u8 sel, __scal_buf_addr_t *addr)
// description     : scaler change frame buffer address, only change start address parameters
// parameters    :
//                 sel <scaler select>
//                 addr  <frame buffer address for 3 channel, 32 bit absolute address>
// return            :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Fb_Addr(__u8 sel, __scal_buf_addr_t *addr)

{
    scal_dev[sel]->buf_addr[0] = addr->ch0_addr+ de_scal_ch0_offset;
    scal_dev[sel]->buf_addr[1] = addr->ch1_addr+ de_scal_ch1_offset;
    scal_dev[sel]->buf_addr[2] = addr->ch2_addr+ de_scal_ch2_offset;

    return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_Init_Phase(__u8 sel, __scal_scan_mod_t *in_scan, __scal_src_size_t *in_size,
//                                           __scal_src_type_t *in_type, __scal_scan_mod_t *out_scan,
//                                           __scal_out_size_t *out_size, __scal_out_type_t *out_type, __u8 dien)
// description     : set scaler init phase according to in/out information
// parameters    :
//                 sel <scaler select>
//                 in_scan <scale src data scan mode, if deinterlaceing open, the scan mode is progressive for scale>
//                 in_size <scale region define,  src size, offset, scal size>
//                 in_type <src data type>
//                 out_scan <scale output data scan mode>
//                 out_size <scale out size>
//                 out_type <output data format>
//                 dien <deinterlace enable>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Init_Phase(__u8 sel, __scal_scan_mod_t *in_scan, __scal_src_size_t *in_size,
                             __scal_src_type_t *in_type, __scal_scan_mod_t *out_scan,
                             __scal_out_size_t *out_size, __scal_out_type_t *out_type, __u8 dien)

{
     __s32 ch0_h_phase=0, ch0_v_phase0=0, ch0_v_phase1=0, ch12_h_phase=0, ch12_v_phase0=0, ch12_v_phase1=0;
	 __u8 h_shift=0, w_shift=0;
     __s32 in_h0, in_h1, out_h0, out_h1;


     //set register value
     scal_dev[sel]->output_fmt.scan_mod = out_scan->field;
     scal_dev[sel]->input_fmt.scan_mod = out_scan->field ? 0x0 : in_scan->field;  //out scan and in scan are not valid at the same time
     scal_dev[sel]->field_ctrl.field_loop_mod = 0x0;
     scal_dev[sel]->field_ctrl.valid_field_cnt = 0x1-0x1;
     scal_dev[sel]->field_ctrl.field_cnt = in_scan->bottom;

     if(sel == 1)  //initphase only for sc0 valid. for sc1, outinterlace is implemented no by initphase. for sc1, the input initphase is not supported
        return 0;

     //sampling method, phase
	 if(in_type->fmt == DE_SCAL_INYUV420)
	 {
	     if(in_type->sample_method == 0x0)  //
	     {
	         ch0_h_phase = 0x0;
			 ch0_v_phase0 = 0x0;
			 ch0_v_phase1 = 0x0;
			 ch12_h_phase = 0xfc000;   //-0.25
			 ch12_v_phase0 = 0xfc000;  //-0.25
			 ch12_v_phase1 = 0xfc000;  //-0.25
	     }
		 else
		 {
		     ch0_h_phase = 0x0;
			 ch0_v_phase0 = 0x0;
			 ch0_v_phase1 = 0x0;
			 ch12_h_phase = 0x0;       //0
			 ch12_v_phase0 = 0xfc000;  //-0.25
			 ch12_v_phase1 = 0xfc000;  //-0.25
		 }
	 }
	 else  //can added yuv411 or yuv420 init phase for sample method
	 {
	     ch0_h_phase = 0x0;
		 ch0_v_phase0 = 0x0;
		 ch0_v_phase1 = 0x0;
		 ch12_h_phase = 0x0;
		 ch12_v_phase0 = 0x0;
		 ch12_v_phase1 = 0x0;
	 }

     //location offset
     w_shift = (in_type->fmt == DE_SCAL_INYUV420 || in_type->fmt == DE_SCAL_INYUV422) ? 0x1 : ((in_type->fmt == DE_SCAL_INYUV411) ? 0x2 : 0x0);
	 h_shift = (in_type->fmt == DE_SCAL_INYUV420 || in_type->fmt == DE_SCAL_INCSIRGB) ? 0x1 : 0x0;

     //deinterlace and in scan mode enable, //dy
     if(((dien == 0x01) || (in_scan->field== 0x1)) && (in_size->y_off & 0x1)&& (in_scan->bottom == 0x0))  //
     {
         ch0_v_phase0 = (ch0_v_phase0 + 0x10000) & SCALINITPASELMT;
         ch12_v_phase0 = (ch12_v_phase0 + 0x10000) & SCALINITPASELMT;
     }
     else
     {
         ch12_v_phase0 = (ch12_v_phase0 + (in_size->y_off & ((1<<h_shift)-1))*(0x10000>>h_shift)) & SCALINITPASELMT;
         ch12_v_phase1 = ch12_v_phase0;
     }

	 //dx
	 scal_dev[sel]->ch0_h_init_phase.phase= ch0_h_phase;
	 scal_dev[sel]->ch12_h_init_phase.phase = (ch12_h_phase + (in_size->x_off & ((1<<w_shift) - 1)) * (0x10000>>w_shift)) & SCALINITPASELMT;

     //outinterlace,
     if(out_scan->field == 0x1)  //outinterlace enable
     {
         in_h0 = in_size->scal_height;
         in_h1 = (in_type->fmt == DE_SCAL_INYUV420) ? (in_h0+0x1)>>1: in_h0;
         out_h0 = out_size->height;
         out_h1 = (out_type->fmt == DE_SCAL_OUTPYUV420) ? (out_h0+0x1)>>1 : out_h0;
         if(in_scan->bottom == 0x0)
         {
             scal_dev[sel]->ch0_v_init_phase0.phase= ch0_v_phase0;
             scal_dev[sel]->ch0_v_init_phase1.phase = ch0_v_phase0 + ((in_h0>>in_scan->field)<<16)/(out_h0);
             scal_dev[sel]->ch12_v_init_phase0.phase = ch12_v_phase0;
             scal_dev[sel]->ch12_v_init_phase1.phase = ch12_v_phase0 + ((in_h1>>in_scan->field)<<16)/(out_h1);
         }
         else
         {
             scal_dev[sel]->ch0_v_init_phase0.phase = ch0_v_phase1;
             scal_dev[sel]->ch0_v_init_phase1.phase = ch0_v_phase1 + ((in_h0>>in_scan->field)<<16)/(out_h0);
             scal_dev[sel]->ch12_v_init_phase0.phase = ch12_v_phase1;
             scal_dev[sel]->ch12_v_init_phase1.phase = ch12_v_phase1 + ((in_h1>>in_scan->field)<<16)/(out_h1);
         }
     }
     else  //outinterlace disable
     {
         scal_dev[sel]->ch0_v_init_phase0.phase = ch0_v_phase0;
         scal_dev[sel]->ch0_v_init_phase1.phase = ch0_v_phase1;
         scal_dev[sel]->ch12_v_init_phase0.phase = ch12_v_phase0;
         scal_dev[sel]->ch12_v_init_phase1.phase = ch12_v_phase1;

     }

	 return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_Scaling_Factor(__u8 sel, __scal_scan_mod_t *in_scan, __scal_src_size_t *in_size,
//                                               __scal_src_type_t *in_type, __scal_scan_mod_t *out_scan,
//                                               __scal_out_size_t *out_size, __scal_out_type_t *out_type)
// description      : set scaler scaling factor, modify algorithm and tape offset
// parameters       :
//                 sel <scaler select>
//                 in_scan <scale src data scan mode, if deinterlaceing open, the scan mode is progressive for scale>
//                 in_size <scale region define,  src size, offset, scal size>
//                 in_type <src data type>
//                 out_scan <scale output data scan mode>
//                 out_size <scale out size, when output interlace, the height is 2xoutheight ,for example 480i, the value is 480>
//                 out_type <output data format>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Scaling_Factor(__u8 sel, __scal_scan_mod_t *in_scan, __scal_src_size_t *in_size,
                                 __scal_src_type_t *in_type, __scal_scan_mod_t *out_scan,
                                 __scal_out_size_t *out_size, __scal_out_type_t *out_type)

{
    __s32 in_w0, in_h0, in_w1, in_h1, out_w0, out_h0, out_w1, out_h1;
    __s32 ch0_hstep, ch0_vstep, ch1_hstep, ch1_vstep;
	__u32 w_shift, h_shift;

    in_w0 = in_size->scal_width;
    in_h0 = in_size->scal_height;

    out_w0 = out_size->width;
    out_h0 = out_size->height + (out_scan->field & 0x1);	//modify by zchmin 110317

	if(sel == 1)  //for sc1
	{
		//for insize modify
		w_shift = (in_type->fmt == DE_SCAL_INYUV411) ? 2 : (in_type->fmt == DE_SCAL_INYUV420)||(in_type->fmt == DE_SCAL_INYUV422) ? 1 : 0;
		h_shift = (in_type->fmt == DE_SCAL_INYUV420) || (in_type->fmt == DE_SCAL_INCSIRGB) ? 1 : 0;
		in_w0 = (in_w0>>w_shift)<<w_shift;
		in_h0 = (in_h0>>h_shift)<<h_shift;
		//for outsize modify
		w_shift = (out_type->fmt == DE_SCAL_OUTPYUV411) ? 2 : (out_type->fmt == DE_SCAL_OUTPYUV420)||(out_type->fmt == DE_SCAL_OUTPYUV422) ? 1 : 0;
		h_shift = (out_type->fmt == DE_SCAL_OUTPYUV420) ? 1 : 0;
		out_w0 = (out_w0>>w_shift)<<w_shift;
		out_h0 = (out_h0>>h_shift)<<h_shift;
		if(out_w0>SCALLINEMAX)
			out_w0 = SCALLINEMAX;  //here
		ch0_hstep = (in_w0<<16)/out_w0;
	    ch0_vstep = ((in_h0>>in_scan->field)<<16)/( out_h0);
	    scal_dev[sel]->ch0_h_factor.factor= ch0_hstep;
	    scal_dev[sel]->ch0_v_factor.factor = ch0_vstep<<(out_scan->field);
		return 0;
	}

	//sc0
    if((in_type->mod == DE_SCAL_INTER_LEAVED) && (in_type->fmt == DE_SCAL_INYUV422))
    {
        in_w0 &=0xfffffffe;
    }
    //algorithm select
    if(out_w0 > SCALLINEMAX)
    {
        scal_dev[sel]->agth_sel.linebuf_agth= 0x1;
        if(in_w0>SCALLINEMAX)  //
        {
            in_w0 = SCALLINEMAX;
        }
    }
    else
    {
        scal_dev[sel]->agth_sel.linebuf_agth= 0x0;
    }

    //channel 1,2 size
    if((in_type->fmt == DE_SCAL_INYUV420) || (in_type->fmt == DE_SCAL_INYUV422))
    {
        in_w1 = (in_w0 + 0x1)>>0x1;
    }
    else if(in_type->fmt == DE_SCAL_INYUV411)
    {
        in_w1 = (in_w0 + 0x3)>>0x2;
    }
    else
    {
        in_w1 = in_w0;
    }
    if((in_type->fmt == DE_SCAL_INYUV420) || (in_type->fmt == DE_SCAL_INCSIRGB))
    {
        in_h1 = (in_h0 + 0x1)>>0x1;
    }
    else
    {
        in_h1 = in_h0;
    }
    if((out_type->fmt == DE_SCAL_OUTPYUV420) || (out_type->fmt == DE_SCAL_OUTPYUV422))
    {
        out_w1 = (out_w0 + 0x1)>>0x1;
    }
    else if(out_type->fmt == DE_SCAL_OUTPYUV411)
    {
        out_w1 = (out_w0 + 0x3)>>0x2;
    }
    else
    {
        out_w1 = out_w0;
    }
    if(out_type->fmt == DE_SCAL_INYUV420)
    {
        out_h1 = (out_h0+ 0x1)>>0x1;
    }
    else
    {
        out_h1 = out_h0;
    }
    //step factor
    ch0_hstep = (in_w0<<16)/out_w0;
    ch0_vstep = ((in_h0>>in_scan->field)<<16)/( out_h0 );
    ch1_hstep = (in_w1<<16)/out_w1;
    ch1_vstep = ((in_h1>>in_scan->field)<<16)/(out_h1);
    scal_dev[sel]->ch0_h_factor.factor= ch0_hstep;
    scal_dev[sel]->ch0_v_factor.factor = ch0_vstep<<(out_scan->field);
    scal_dev[sel]->ch12_h_factor.factor = ch1_hstep;
    scal_dev[sel]->ch12_v_factor.factor = ch1_vstep<<(out_scan->field);

    //tape offset setting
    iDE_SCAL_Set_Tape_Offset(sel, 0x0, 0, 0, ch0_hstep);
    iDE_SCAL_Set_Tape_Offset(sel, 0x0, 0, 1, ch0_vstep);
    iDE_SCAL_Set_Tape_Offset(sel, 0x0, 1, 0, ch1_hstep);
    iDE_SCAL_Set_Tape_Offset(sel, 0x0, 1, 1, ch1_vstep);

	return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_Scaling_Coef(__u8 sel, __scal_scan_mod_t *in_scan, __scal_src_size_t *in_size,
//                                             __scal_src_type_t *in_type, __scal_scan_mod_t *out_scan,
//                                             __scal_out_size_t *out_size, __scal_out_type_t *out_type, __u8 smth_mode)
// description      : set scaler scaling filter coefficients
// parameters       :
//                 sel <scaler select>
//                 in_scan <scale src data scan mode, if deinterlaceing open, the scan mode is progressive for scale>
//                 in_size <scale region define,  src size, offset, scal size>
//                 in_type <src data type>
//                 out_scan <scale output data scan mode>
//                 out_size <scale out size>
//                 out_type <output data format>
//                 smth_mode <scaler filter effect select>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Scaling_Coef(__u8 sel, __scal_scan_mod_t *in_scan, __scal_src_size_t *in_size,
                               __scal_src_type_t *in_type, __scal_scan_mod_t *out_scan,
                               __scal_out_size_t *out_size, __scal_out_type_t *out_type, __u8 smth_mode)

{
    __s32 in_w0, in_h0, in_w1, in_h1, out_w0, out_h0, out_w1, out_h1;
    __s32 ch0h_smth_level, ch0v_smth_level, ch1h_smth_level, ch1v_smth_level;
    __u32 int_part, float_part;
    __u32 zoom0_size, zoom1_size, zoom2_size, zoom3_size, zoom4_size, zoom5_size, al1_size;
    __u32 ch0h_sc, ch0v_sc, ch1h_sc, ch1v_sc;
    __u32 ch0v_fir_coef_addr, ch0h_fir_coef_addr, ch1v_fir_coef_addr, ch1h_fir_coef_addr;
    __u32 ch0v_fir_coef_ofst, ch0h_fir_coef_ofst, ch1v_fir_coef_ofst, ch1h_fir_coef_ofst;
    __s32 fir_ofst_tmp;
    __u32 i;

    in_w0 = in_size->scal_width;
    in_h0 = in_size->scal_height;

    out_w0 = out_size->width;
    out_h0 = out_size->height;

    zoom0_size = 1;
    zoom1_size = 8;
    zoom2_size = 4;
    zoom3_size = 1;
    zoom4_size = 1;
    zoom5_size = 1;
    al1_size = zoom0_size + zoom1_size + zoom2_size + zoom3_size + zoom4_size + zoom5_size;

    if((in_type->mod == DE_SCAL_INTER_LEAVED) && (in_type->fmt == DE_SCAL_INYUV422))
    {
        in_w0 &=0xfffffffe;
    }

    //channel 1,2 size
    if((in_type->fmt == DE_SCAL_INYUV420) || (in_type->fmt == DE_SCAL_INYUV422))
    {
        in_w1 = (in_w0 + 0x1)>>0x1;
    }
    else if(in_type->fmt == DE_SCAL_INYUV411)
    {
        in_w1 = (in_w0 + 0x3)>>0x2;
    }
    else
    {
        in_w1 = in_w0;
    }
    if((in_type->fmt == DE_SCAL_INYUV420) || (in_type->fmt == DE_SCAL_INCSIRGB))
    {
        in_h1 = (in_h0 + 0x1)>>0x1;
    }
    else
    {
        in_h1 = in_h0;
    }
    if((out_type->fmt == DE_SCAL_OUTPYUV420) || (out_type->fmt == DE_SCAL_OUTPYUV422))
    {
        out_w1 = (out_w0 + 0x1)>>0x1;
    }
    else if(out_type->fmt == DE_SCAL_OUTPYUV411)
    {
        out_w1 = (out_w0 + 0x3)>>0x2;
    }
    else
    {
        out_w1 = out_w0;
    }
    if(out_type->fmt == DE_SCAL_INYUV420)
    {
        out_h1 = (out_h0+ 0x1)>>0x1;
    }
    else
    {
        out_h1 = out_h0;
    }

    //
    //smooth level for channel 0,1 in vertical and horizontal direction
    ch0h_smth_level = (smth_mode&0x40)  ?  0 - (smth_mode&0x3f) : smth_mode&0x3f;
    ch0v_smth_level = ch0h_smth_level;
    if((smth_mode>>7) &0x01)
    {
      ch0v_smth_level = (smth_mode&0x4000) ? 0 - ((smth_mode&0x3f00)>>8) : ((smth_mode&0x3f00)>>8);
    }
    if((smth_mode>>31)&0x01)
    {
      ch1h_smth_level = (smth_mode&0x400000) ? 0 - ((smth_mode&0x3f0000)>>16) : ((smth_mode&0x3f0000)>>16);
      ch1v_smth_level = ch1h_smth_level;
      if((smth_mode >> 23)&0x1)
      {
        ch1v_smth_level = (smth_mode&0x40000000) ? 0 - ((smth_mode&0x3f000000)>>24) : ((smth_mode&0x3f000000)>>24);
      }
    }
    //
    ch0h_sc = (in_w0<<3)/out_w0;
    ch0v_sc = ((in_h0>>in_scan->field)<<3)/(out_h0);
    ch1h_sc = (in_w1<<3)/out_w1;
    ch1v_sc = ((in_h1>>in_scan->field)<<3)/(out_h1);

    //modify ch1 smooth level according to ratio to ch0
    if(((smth_mode>>31)&0x01)==0x0)
    {
      if(!ch1h_sc)
      {
        ch1h_smth_level = 0;
      }
      else
      {
        ch1h_smth_level = ch0h_smth_level>>(ch0h_sc/ch1h_sc);
      }

      if(!ch1v_sc)
      {
        ch1v_smth_level = 0;
      }
      else
      {
        ch1v_smth_level = ch0v_smth_level>>(ch0v_sc/ch1v_sc);
      }
    }

      //comput the fir coefficient offset in coefficient table
      int_part = ch0v_sc>>3;
      float_part = ch0v_sc & 0x7;
      ch0v_fir_coef_ofst = (int_part==0)  ? zoom0_size :
                           (int_part==1)  ? zoom0_size + float_part :
                           (int_part==2)  ? zoom0_size + zoom1_size + (float_part>>1) :
                           (int_part==3)  ? zoom0_size + zoom1_size + zoom2_size :
                           (int_part==4)  ? zoom0_size + zoom1_size + zoom2_size +zoom3_size :
                           zoom0_size + zoom1_size + zoom2_size + zoom3_size + zoom4_size;
      int_part = ch0h_sc>>3;
      float_part = ch0h_sc & 0x7;
      ch0h_fir_coef_ofst = (int_part==0)  ? zoom0_size :
                           (int_part==1)  ? zoom0_size + float_part :
                           (int_part==2)  ? zoom0_size + zoom1_size + (float_part>>1) :
                           (int_part==3)  ? zoom0_size + zoom1_size + zoom2_size :
                           (int_part==4)  ? zoom0_size + zoom1_size + zoom2_size +zoom3_size :
                           zoom0_size + zoom1_size + zoom2_size + zoom3_size + zoom4_size;
      int_part = ch1v_sc>>3;
      float_part = ch1v_sc & 0x7;
      ch1v_fir_coef_ofst = (int_part==0)  ? zoom0_size :
                           (int_part==1)  ? zoom0_size + float_part :
                           (int_part==2)  ? zoom0_size + zoom1_size + (float_part>>1) :
                           (int_part==3)  ? zoom0_size + zoom1_size + zoom2_size :
                           (int_part==4)  ? zoom0_size + zoom1_size + zoom2_size +zoom3_size :
                           zoom0_size + zoom1_size + zoom2_size + zoom3_size + zoom4_size;
      int_part = ch1h_sc>>3;
      float_part = ch1h_sc & 0x7;
      ch1h_fir_coef_ofst =  (int_part==0)  ? zoom0_size :
                            (int_part==1)  ? zoom0_size + float_part :
                            (int_part==2)  ? zoom0_size + zoom1_size + (float_part>>1) :
                            (int_part==3)  ? zoom0_size + zoom1_size + zoom2_size :
                            (int_part==4)  ? zoom0_size + zoom1_size + zoom2_size +zoom3_size :
                            zoom0_size + zoom1_size + zoom2_size + zoom3_size + zoom4_size;
    //added smooth level for each channel in horizontal and vertical direction
    fir_ofst_tmp = ch0v_fir_coef_ofst + ch0v_smth_level;
    ch0v_fir_coef_ofst = (fir_ofst_tmp<0) ? 0 : fir_ofst_tmp;
    fir_ofst_tmp = ch0h_fir_coef_ofst + ch0h_smth_level;
    ch0h_fir_coef_ofst = (fir_ofst_tmp<0) ? 0 : fir_ofst_tmp;
    fir_ofst_tmp = ch1v_fir_coef_ofst + ch1v_smth_level;
    ch1v_fir_coef_ofst = (fir_ofst_tmp<0) ? 0 : fir_ofst_tmp;
    fir_ofst_tmp = ch1h_fir_coef_ofst + ch1h_smth_level;
    ch1h_fir_coef_ofst = (fir_ofst_tmp<0) ? 0 : fir_ofst_tmp;
    //modify coefficient offset
    ch0v_fir_coef_ofst = (ch0v_fir_coef_ofst > (al1_size - 1)) ? (al1_size - 1) : ch0v_fir_coef_ofst;
    ch1v_fir_coef_ofst = (ch1v_fir_coef_ofst > (al1_size - 1)) ? (al1_size - 1) : ch1v_fir_coef_ofst;
    ch0h_fir_coef_ofst = (ch0h_fir_coef_ofst > (al1_size - 1)) ? (al1_size - 1) : ch0h_fir_coef_ofst;
    ch1h_fir_coef_ofst = (ch1h_fir_coef_ofst > (al1_size - 1)) ? (al1_size - 1) : ch1h_fir_coef_ofst;

    //compute the fir coeficient address for each channel in horizontal and vertical direction
    ch0v_fir_coef_addr = (ch0v_fir_coef_ofst<<7);
    ch0h_fir_coef_addr = (ch0h_fir_coef_ofst<<7);
    ch1v_fir_coef_addr = (ch1v_fir_coef_ofst<<7);
    ch1h_fir_coef_addr = (ch1h_fir_coef_ofst<<7);

    for(i=0; i<32; i++)
    {
        scal_dev[sel]->ch0_h_fir_coef[i] = fir_tab[(ch0h_fir_coef_addr>>2) + i];
        scal_dev[sel]->ch0_v_fir_coef[i] = fir_tab[(ch0v_fir_coef_addr>>2) + i];
        scal_dev[sel]->ch12_h_fir_coef[i] = fir_tab[(ch1h_fir_coef_addr>>2) + i];
        scal_dev[sel]->ch12_v_fir_coef[i] = fir_tab[(ch1v_fir_coef_addr>>2) + i];
    }

    scal_dev[sel]->frm_ctrl.coef_rdy_en = 0x1;

    return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_CSC_Coef(__u8 sel, __u8 in_csc_mode, __u8 out_csc_mode, __u8 incs, __u8 outcs)
// description      : set scaler input/output color space convert coefficients
// parameters       :
//                 sel <scaler select>
//                 in_csc_mode <color space select, bt601, bt709, ycc, xycc>
//                 out_csc_mode <color space select, bt601, bt709, ycc, xycc>
//                 incs <source color space>
//                 |    0  rgb
//                 |    1  yuv
//                 outcs <destination color space>
//                 |    0  rgb
//                 |    1  yuv
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_CSC_Coef(__u8 sel, __u8 in_csc_mode, __u8 out_csc_mode, __u8 incs, __u8 outcs)
{
    __u8  csc_pass;
    __u32 csc_coef_addr;
    __u32 i;

    //compute csc bypass enable
    if(incs == 0x0)  //rgb
    {
        if(outcs == 0x0) //rgb
        {
            csc_pass = 0x01;
            csc_coef_addr = (((in_csc_mode&0x3)<<7) + ((in_csc_mode&0x3)<<6)) + 0x60;
        }
        else
        {
            csc_pass = 0x0;
            csc_coef_addr = (((in_csc_mode&0x3)<<7) + ((in_csc_mode&0x3)<<6)) + 0x60 + 0x30;
        }
    }
    else
    {
        if(outcs == 0x0)
        {
            csc_pass = 0x00;
            csc_coef_addr = (((in_csc_mode&0x3)<<7) + ((in_csc_mode&0x3)<<6));
        }
        else
        {
            csc_pass = 0x01;
            csc_coef_addr = (((in_csc_mode&0x3)<<7) + ((in_csc_mode&0x3)<<6)) + 0x30;
        }
    }

    if(!csc_pass)
    {
      for(i=0; i<12; i++)
        {
            scal_dev[sel]->csc_coef[i]= csc_tab[(csc_coef_addr>>2) + i];
        }
    }
    scal_dev[sel]->bypass.csc_bypass_en = csc_pass;
    return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_Out_Format(__u8 sel, __scal_out_type_t *out_type)
// description      : set scaler set output format
// parameters       :
//                 sel <scaler select>
//                 out_type <output data format>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Out_Format(__u8 sel, __scal_out_type_t *out_type)

{
    scal_dev[sel]->output_fmt.byte_seq= out_type->byte_seq;
    scal_dev[sel]->output_fmt.data_fmt = out_type->fmt;
    return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_Out_Size(__u8 sel, __scal_scan_mod_t *out_scan, __scal_out_type_t *out_type,
//                                         __scal_out_size_t *out_size)
// description      : set scaler set output size
// parameters       :
//                 sel <scaler select>
//                 out_scan <output data scan mode>
//                 out_type <output data format>
//                 out_size <scale out size>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Out_Size(__u8 sel, __scal_scan_mod_t *out_scan, __scal_out_type_t *out_type,
                           __scal_out_size_t *out_size)
{
    __u32 w_shift, h_shift, out_w1, out_h1;
	if(sel == 1)  //sc1
	{
		w_shift = (out_type->fmt == DE_SCAL_OUTPYUV411) ? 2 : (out_type->fmt == DE_SCAL_OUTPYUV422)||(out_type->fmt ==
			       DE_SCAL_OUTPYUV420) ? 1 : 0;
		h_shift = (out_type->fmt == DE_SCAL_OUTPYUV420) ? 1 : 0;
		scal_dev[sel]->ch0_out_size.height = (((out_size->height>>h_shift)<<h_shift)>>out_scan->field) - 1;
	    scal_dev[sel]->ch0_out_size.width = ((out_size->width>>w_shift)<<w_shift)- 1;

		return 0;
	}

	//sc0
    if((out_type->fmt == DE_SCAL_OUTPYUV420) || (out_type->fmt == DE_SCAL_OUTPYUV422))
    {
        out_w1 = (out_size->width+ 0x1) >> 1;
    }
    else if(out_type->fmt == DE_SCAL_OUTPYUV411)
    {
        out_w1 = (out_size->width+ 0x3) >> 2;
    }
    else
    {
        out_w1 = out_size->width;
    }

    if(out_type->fmt == DE_SCAL_OUTPYUV420)
    {
        out_h1 = (out_size->height + 0x1) >> 1;
    }
    else
    {
        out_h1 = out_size->height;
    }

    scal_dev[sel]->ch0_out_size.height = ((out_size->height + (out_scan->field & 0x1))>>out_scan->field) - 1;
    scal_dev[sel]->ch0_out_size.width = out_size->width - 1;
    scal_dev[sel]->ch12_out_size.height = ((out_h1 + (out_scan->field & 0x1)) >>out_scan->field) - 1;
    scal_dev[sel]->ch12_out_size.width = out_w1 - 1;
    return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_Trig_Line(__u8 sel, __u32 line)
// description      : set scaler output trigger line
// parameters       :
//                 sel <scaler select>, //un support
//                 line <line number, only valid for scaler output to display>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Trig_Line(__u8 sel, __u32 line)

{
    scal_dev[sel]->line_int_ctrl.field_sel = 0x0;
    scal_dev[sel]->line_int_ctrl.trig_line = line;
    return 0;
}

//*********************************************************************************************
// function         : DE_SCAL_Set_Int_En(__u8 sel, __u32 int_num)
// description      : set scaler interrupt enable bit
// parameters       :
//                 sel <scaler select>, //un support
//                 int_num <7, 9, 10>
//                 |    7   write back interrupt
//                 |    9   line interrupt
//                 |    10  register ready load interrupt
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Int_En(__u8 sel, __u32 int_num)

{
    if(int_num == 7)
    {
        scal_dev[sel]->int_en.wb_en = 0x1;
    }
    else if(int_num == 9)
    {
        scal_dev[sel]->int_en.line_en = 0x1;
    }
    else if(int_num == 10)
    {
        scal_dev[sel]->int_en.load_en = 0x1;
    }

    return 0;
}

//*********************************************************************************************
// function         : DE_SCAL_Set_Di_Ctrl(__u8 sel, __u8 en, __u8 mode, __u8 diagintp_en, __u8 tempdiff_en)
// description      : set scaler deinterlace control parameter
// parameters       :
//                 sel <scaler select>,
//                 en <0,1>
//                 |    0  deinterlace disable
//                 |    1  deinterlace enable
//                 mode <0,1,2,3>
//                 |    0   weave
//                 |    1   bob
//                 |    2   maf
//                 |    3   maf-bob
//                 diagintp_en <0, 1>
//                 tempdiff_en <0,1>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Di_Ctrl(__u8 sel, __u8 en, __u8 mode, __u8 diagintp_en, __u8 tempdiff_en)
{

	if(sel == 1) //sc1
		return 0;
    scal_dev[sel]->di_ctrl.en = en;
    scal_dev[sel]->di_ctrl.mod = mode;
    scal_dev[sel]->di_ctrl.diagintp_en = diagintp_en;
    scal_dev[sel]->di_ctrl.tempdiff_en = tempdiff_en;
    return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_Di_PreFrame_Addr(__u8 sel, __u32 addr)
// description      : set scaler deinterlace pre frame luma address
// parameters       :
//                 sel <scaler select>,
//                 addr <pre frame address>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Di_PreFrame_Addr(__u8 sel, __u32 addr)
{
    if(sel == 1)
		return 0;
    scal_dev[sel]->di_preluma_buf = addr;
    return 0;
}

//*********************************************************************************************
// function         :  DE_SCAL_Set_Di_MafFlag_Src(__u8 sel, __u32 addr, __u32 stride)
// description      : set scaler deinterlace maf flag address and linestride
// parameters       :
//                 sel <scaler select>,
//                 addr <maf flag address>
//		 stride <maf line stride>
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_Di_MafFlag_Src(__u8 sel, __u32 addr, __u32 stride)

{
    if(sel == 1) //sc1
	    return 0;
    scal_dev[sel]->di_mafflag_buf= addr;
    scal_dev[sel]->di_flag_linestride = stride;
    return 0;
}


//**********************************************************************************
// function         : DE_SCAL_Start(__u8 sel)
// description      : scaler module  start set
// parameters       :
//                 sel <scaler select>
// return           : success
//***********************************************************************************
__s32 DE_SCAL_Start(__u8 sel)
{
    scal_dev[sel]->frm_ctrl.frm_start = 0x1;

    return 0;
}

//**********************************************************************************
// function         : DE_SCAL_Set_Filtercoef_Ready(__u8 sel)
// description      : scaler filter coefficient set ready
// parameters       :
//                 sel <scaler select>
// return           : success
//***********************************************************************************
__s32 DE_SCAL_Set_Filtercoef_Ready(__u8 sel)

{
    scal_dev[sel]->frm_ctrl.coef_rdy_en = 0x1;

    return 0;
}

//**********************************************************************************
// function         : DE_SCAL_Set_Reg_Rdy(__u8 sel)
// description      : scaler configure registers set ready
// parameters       :
//                 sel <scaler select>
// return           : success
//***********************************************************************************
__s32 DE_SCAL_Set_Reg_Rdy(__u8 sel)

{
    scal_dev[sel]->frm_ctrl.reg_rdy_en = 0x1;

    return 0;
}


//**********************************************************************************
// function         : DE_SCAL_Reset(__u8 sel)
// description      : scaler module reset(reset module status machine)
// parameters       :
//                 sel <scaler select>
// return            : success
//***********************************************************************************
__s32 DE_SCAL_Reset(__u8 sel)

{
    scal_dev[sel]->frm_ctrl.frm_start = 0x0;

    return 0;
}

//**********************************************************************************
// function         : DE_SCAL_Output_Port_Select(__u8 sel, __u8 port)
// description      : scaler output to be enable
// parameters       :
//                 sel <scaler select>
//                 port <scaler output port>
//                 |    0   image0
//                 |    1   image1
//                 |    2   mixer
// return            : success
//***********************************************************************************
__s32 DE_SCAL_Output_Port_Select(__u8 sel, __u8 port)

{
    scal_dev[sel]->frm_ctrl.out_port_sel= port;

    return 0;
}


//**********************************************************************************
// function         : DE_SCAL_Output_Select(__u8 sel)
// description      : scaler output select
// parameters       :
//                 sel <scaler select>
//                 out<0:be0; 1:be1; 2:me; 3:writeback>
// return            : success
//***********************************************************************************
__s32 DE_SCAL_Output_Select(__u8 sel, __u8 out)
{
    if(out == 3)//write back
    {
        scal_dev[sel]->frm_ctrl.out_ctrl = 1;//disable scaler output to be/me
        scal_dev[sel]->frm_ctrl.out_port_sel = 0;
    }
    else if(out < 3)
    {
        scal_dev[sel]->frm_ctrl.out_ctrl = 0;//enable scaler output to be/me
        scal_dev[sel]->frm_ctrl.out_port_sel = out;
    }

    return 0;
}

//**********************************************************************************
// function         : DE_SCAL_Writeback_Enable(__u8 sel)
// description      : scaler write back enable
// parameters       :
//                 sel <scaler select>
// return            : success
//***********************************************************************************
__s32 DE_SCAL_Writeback_Enable(__u8 sel)

{
    scal_dev[sel]->frm_ctrl.wb_en = 0x1;

    return 0;
}

//**********************************************************************************
// function         : DE_SCAL_Writeback_Disable(__u8 sel)
// description      : scaler write back enable
// parameters       :
//                sel <0,1>
//                |    0  scaler0
//                |    1  scaler1
// return            : success
//***********************************************************************************
__s32 DE_SCAL_Writeback_Disable(__u8 sel)
{
	scal_dev[sel]->frm_ctrl.wb_en = 0x0;

	return 0;
}

//**********************************************************************************
// function         : DE_SCAL_Enable(__u8 sel)
// description      : scaler module enable
// parameters       :
//                 sel <scaler select>
// return           : success
//***********************************************************************************
__s32 DE_SCAL_Enable(__u8 sel)

{
    scal_dev[sel]->modl_en.en = 0x1;
    //scal_dev[sel]->field_ctrl.sync_edge= 0x1;

    return 0;
}

//**********************************************************************************
// function         : DE_SCAL_Disable(__u8 sel)
// description      : scaler module disable
// parameters       :
//                 sel <scaler select>
// return           : success
//***********************************************************************************
__s32 DE_SCAL_Disable(__u8 sel)

{
    scal_dev[sel]->modl_en.en = 0x0;

    return 0;
}


//**********************************************************************************
// function         : DE_SCAL_Set_Writeback_Addr(__u8 sel, __scal_buf_addr_t *addr)
// description      : scaler write back address set
// parameters       :
//                 sel <scaler select>
//                 addr <address for wb>
// return            : success
//***********************************************************************************
__s32 DE_SCAL_Set_Writeback_Addr(__u8 sel, __scal_buf_addr_t *addr)

{
    scal_dev[sel]->wb_addr[0] = addr->ch0_addr;
    scal_dev[sel]->wb_addr[1] = addr->ch1_addr;
    scal_dev[sel]->wb_addr[2] = addr->ch2_addr;


    return 0;
}


//*********************************************************************************
// function         : DE_SCAL_Get_Input_Format(__u8 sel)
// description      : scaler input format get
// parameters       :
//                sel <scaler select>
// return             :
//                format<0,1,2,3,4,5>
//                |    0  yuv444
//                |    1  yuv422
//                |    2  yuv420
//                |    3  yuv411
//                |    4  csirgb
//                |    5  rgb888
//*********************************************************************************
__u8 DE_SCAL_Get_Input_Format(__u8 sel)
{
    __u8 fmt_ret;
    fmt_ret = scal_dev[sel]->input_fmt.data_fmt;

    return fmt_ret;
}

//*********************************************************************************
// function         : DE_SCAL_Get_Input_Mode(__u8 sel)
// description      : scaler input mode get
// parameters       :
//                sel <scaler select>
// return             :
//                 mode<0,1,2,3,4>
//                 |    0  non-macro block plannar data
//                 |    1  interleaved data
//                 |    2  non-macro block uv combined data
//                 |    3  macro block plannar data
//                 |    4  macro block uv combined data
//*********************************************************************************
__u8 DE_SCAL_Get_Input_Mode(__u8 sel)
{
    __u8 mode_ret;
    mode_ret = scal_dev[sel]->input_fmt.data_mod;

    return mode_ret;
}



//**********************************************************************************
// function         : DE_SCAL_Get_Output_Format(__u8 sel)
// description      : display engine front-end output data format get
// parameters       :
//                sel <scaler select>
//return              :
//                format  <0, 1, 4, 5, 6, 7>
//                |    0  plannar rgb output
//                |    1  interleaved argb ouptut
//                |    4  plannar yuv444
//                |    5  plannar yuv420
//                |    6  plannar yuv422
//                |    7  plannar yuv411
//***********************************************************************************
__u8 DE_SCAL_Get_Output_Format(__u8 sel)
{
    __u8 fmt_ret;
    fmt_ret = scal_dev[sel]->output_fmt.data_fmt;

    return fmt_ret;
}


//*********************************************************************************
// function         : DE_SCAL_Get_Input_Width(__u8 sel)
// description      : scaler input width get
// parameters       :
//                sel <scaler select>
//return              :
//                width  <8~8192>
//*********************************************************************************
__u16 DE_SCAL_Get_Input_Width(__u8 sel)
{
    __u16 in_w;
    in_w = scal_dev[sel]->ch0_in_size.width + 0x1;

    return in_w;
}

//*********************************************************************************
// function         : DE_SCAL_Get_Input_Height(__u8 sel)
// description      : scaler input height get
// parameters       :
//                sel <scaler select>
//return               :
//                 height  <8~8192>
//*********************************************************************************
__u16 DE_SCAL_Get_Input_Height(__u8 sel)
{
    __u16 in_h;
    in_h = scal_dev[sel]->ch0_in_size.height + 0x1;

    return in_h;
}

//*********************************************************************************
// function         : DE_SCAL_Get_Output_Width(__u8 sel)
// description      : scaler output width get
// parameters       :
//                sel <scaler select>
//return              :
//                width  <8~8192>
//*********************************************************************************
__u16 DE_SCAL_Get_Output_Width(__u8 sel)
{
  __u16 out_w;
  out_w = scal_dev[sel]->ch0_out_size.width + 0x1;

  return out_w;
}

//*********************************************************************************
// function         : DE_SCAL_Get_Output_Height(__u8 sel)
// description      : scaler output height get
// parameters       :
//                sel <scaler select>
//return              :
//                height  <8~8192>
//*********************************************************************************
__u16 DE_SCAL_Get_Output_Height(__u8 sel)
{
    __u16 out_h;
    out_h = scal_dev[sel]->ch0_out_size.height + 0x1;

    return out_h;
}

//**********************************************************************************
// function         : DE_SCAL_Get_Start_Status(__u8 sel)
// description      : scaler start status get
// parameters       :
//                sel <0,1>
//                |    0  scaler0
//                |    1  scaler1
// return           :
//                 0  scaler enable
//                 -1 scaler disable
//***********************************************************************************
__s32 DE_SCAL_Get_Start_Status(__u8 sel)
{
     if(scal_dev[sel]->modl_en.en  && scal_dev[sel]->frm_ctrl.frm_start)
     {
         return 0;
     }
     else
     {
         return -1;
     }
}

//**********************************************************************************
// function         : DE_SCAL_Get_Field_Status(__u8 sel)
// description      : lcd field status
// parameters       :
//                sel <0,1>
//                |    0  scaler0
//                |    1  scaler1
// return           :
//                 0  top field
//                 1  bottom field
//***********************************************************************************

__s32 DE_SCAL_Get_Field_Status(__u8 sel)
{
	return scal_dev[sel]->status.lcd_field;
}


//*********************************************************************************************
// function         : iDE_SCAL_Matrix_Mul(__scal_matrix4x4 in1, __scal_matrix4x4 in2, __scal_matrix4x4 *result)
// description      : matrix multiple of 4x4, m1 * m2
// parameters       :
//                 in1/in2 <4x4 matrix>
//                 result <>
// return           :
//               success
//***********************************************************************************************
__s32 iDE_SCAL_Matrix_Mul(__scal_matrix4x4 in1, __scal_matrix4x4 in2, __scal_matrix4x4 *result)
{
	__scal_matrix4x4 tmp;

	tmp.x00 = (in1.x00 * in2.x00 + in1.x01 * in2.x10 + in1.x02 * in2.x20 + in1.x03 * in2.x30) >> 10;
	tmp.x01 = (in1.x00 * in2.x01 + in1.x01 * in2.x11 + in1.x02 * in2.x21 + in1.x03 * in2.x31) >> 10;
	tmp.x02 = (in1.x00 * in2.x02 + in1.x01 * in2.x12 + in1.x02 * in2.x22 + in1.x03 * in2.x32) >> 10;
	tmp.x03 = (in1.x00 * in2.x03 + in1.x01 * in2.x13 + in1.x02 * in2.x23 + in1.x03 * in2.x33) >> 10;
	tmp.x10 = (in1.x10 * in2.x00 + in1.x11 * in2.x10 + in1.x12 * in2.x20 + in1.x13 * in2.x30) >> 10;
	tmp.x11 = (in1.x10 * in2.x01 + in1.x11 * in2.x11 + in1.x12 * in2.x21 + in1.x13 * in2.x31) >> 10;
	tmp.x12 = (in1.x10 * in2.x02 + in1.x11 * in2.x12 + in1.x12 * in2.x22 + in1.x13 * in2.x32) >> 10;
	tmp.x13 = (in1.x10 * in2.x03 + in1.x11 * in2.x13 + in1.x12 * in2.x23 + in1.x13 * in2.x33) >> 10;
	tmp.x20 = (in1.x20 * in2.x00 + in1.x21 * in2.x10 + in1.x22 * in2.x20 + in1.x23 * in2.x30) >> 10;
	tmp.x21 = (in1.x20 * in2.x01 + in1.x21 * in2.x11 + in1.x22 * in2.x21 + in1.x23 * in2.x31) >> 10;
	tmp.x22 = (in1.x20 * in2.x02 + in1.x21 * in2.x12 + in1.x22 * in2.x22 + in1.x23 * in2.x32) >> 10;
	tmp.x23 = (in1.x20 * in2.x03 + in1.x21 * in2.x13 + in1.x22 * in2.x23 + in1.x23 * in2.x33) >> 10;
	tmp.x30 = (in1.x30 * in2.x00 + in1.x31 * in2.x10 + in1.x32 * in2.x20 + in1.x33 * in2.x30) >> 10;
	tmp.x31 = (in1.x30 * in2.x01 + in1.x31 * in2.x11 + in1.x32 * in2.x21 + in1.x33 * in2.x31) >> 10;
	tmp.x32 = (in1.x30 * in2.x02 + in1.x31 * in2.x12 + in1.x32 * in2.x22 + in1.x33 * in2.x32) >> 10;
	tmp.x33 = (in1.x30 * in2.x03 + in1.x31 * in2.x13 + in1.x32 * in2.x23 + in1.x33 * in2.x33) >> 10;


	*result = tmp;

	return 0;
}


//*********************************************************************************************
// function         : iDE_SCAL_Csc_Lmt(__s32 *value, __s32 min, __s32 max, __s32 shift, __s32 validbit)
// description      : csc coefficient and constant limited
// parameters       :
//                value<coefficient or constant>
//                min/max <limited range>
// return           :
//               success
//***********************************************************************************************
__s32 iDE_SCAL_Csc_Lmt(__s32 *value, __s32 min, __s32 max, __s32 shift, __s32 validbit)
{
    __s32 tmp;
    tmp = (*value)>>shift;
   if(tmp < min)
    *value = min & validbit;
   else if(tmp > max)
     *value = max & validbit;
   else
     *value = tmp & validbit;

   return 0;
}


//*********************************************************************************************
// function         : DE_SCAL_Set_CSC_Coef_Enhance(__u8 sel, __u8 in_csc_mode, __u8 out_csc_mode, __u8 incs, __u8 outcs,
//                                                                __s32  bright, __s32 contrast, __s32 saturaion, __s32 hue)
// description      : set scaler input/output color space convert coefficients
// parameters       :
//                 sel <scaler select>
//                 in_csc_mode <color space select, bt601, bt709, ycc, xycc>
//                 out_csc_mode <color space select, bt601, bt709, ycc, xycc>
//                 incs <source color space>
//                 |    0  rgb
//                 |    1  yuv
//                 outcs <destination color space>
//                 |    0  rgb
//                 |    1  yuv
//                 brightness<0  ~ 63>  default 32
//                 contrast <0 ~ 63> (0.0 ~ 2.0)*32, default 32
//                 saturation<0~ 63> (0.0 ~ 2.0)*32, default 32
//                 hue <0 ~ 63>  default 32
// return           :
//               success
//***********************************************************************************************
__s32 DE_SCAL_Set_CSC_Coef_Enhance(__u8 sel, __u8 in_csc_mode, __u8 out_csc_mode, __u8 incs, __u8 outcs,
                                                   __s32  bright, __s32 contrast, __s32 saturaion, __s32 hue)
{
	__scal_matrix4x4 matrixEn;
	__scal_matrix4x4 matrixconv, *ptmatrix;
	__scal_matrix4x4 matrixresult;
    __s32 *pt;
	__u32 i;
	__s32 sinv, cosv;   //sin_tab: 7 bit fractional

	sinv = image_enhance_tab[8*12 + (hue&0x3f)];
	cosv = image_enhance_tab[8*12 + 8*8 + (hue&0x3f)];

	matrixEn.x00 = contrast << 5;
	matrixEn.x01 = 0;
	matrixEn.x02 = 0;
	matrixEn.x03 = (((bright - 32) + 16) <<10) - ( contrast << 9);
	matrixEn.x10 = 0;
	matrixEn.x11 = (contrast * saturaion * cosv) >> 7;
	matrixEn.x12 = (contrast * saturaion * sinv) >> 7;
	matrixEn.x13 = (1<<17) - ((matrixEn.x11 + matrixEn.x12)<<7);
	matrixEn.x20 = 0;
	matrixEn.x21 = (-contrast * saturaion * sinv)>>7;
	matrixEn.x22 = (contrast * saturaion * cosv) >> 7;
	matrixEn.x23 = (1<<17) - ((matrixEn.x22 + matrixEn.x21)<<7);
	matrixEn.x30 = 0;
	matrixEn.x31 = 0;
	matrixEn.x32 = 0;
	matrixEn.x33 = 1024;

	if((incs == 0) && (outcs == 0))  //rgb to rgb
	{
		ptmatrix = (__scal_matrix4x4 *)((__u32)image_enhance_tab + (in_csc_mode<<7) + 0x40);
		iDE_SCAL_Matrix_Mul(matrixEn, *ptmatrix, &matrixconv);
		ptmatrix = (__scal_matrix4x4 *)((__u32)image_enhance_tab + (in_csc_mode<<7));
		iDE_SCAL_Matrix_Mul(*ptmatrix, matrixconv, &matrixconv);
        matrixresult.x00 = matrixconv.x11;  matrixresult.x01 = matrixconv.x10;
        matrixresult.x02 = matrixconv.x12;  matrixresult.x03 = matrixconv.x13;
        matrixresult.x10 = matrixconv.x01;  matrixresult.x11 = matrixconv.x00;
        matrixresult.x12 = matrixconv.x02;  matrixresult.x13 = matrixconv.x03;
        matrixresult.x20 = matrixconv.x21;  matrixresult.x21 = matrixconv.x20;
        matrixresult.x22 = matrixconv.x22;  matrixresult.x23 = matrixconv.x23;
        matrixresult.x30 = matrixconv.x31;  matrixresult.x31 = matrixconv.x30;
        matrixresult.x32 = matrixconv.x32;  matrixresult.x33 = matrixconv.x33;

	}
	else if((incs == 1) && (outcs == 0)) //yuv to rgb
	{
		ptmatrix = (__scal_matrix4x4 *)((__u32)image_enhance_tab + (in_csc_mode<<7) + 0x40);
		iDE_SCAL_Matrix_Mul(*ptmatrix, matrixEn, &matrixconv);
        matrixresult.x00 = matrixconv.x10;  matrixresult.x01 = matrixconv.x11;
        matrixresult.x02 = matrixconv.x12;  matrixresult.x03 = matrixconv.x13;
        matrixresult.x10 = matrixconv.x00;  matrixresult.x11 = matrixconv.x01;
        matrixresult.x12 = matrixconv.x02;  matrixresult.x13 = matrixconv.x03;
        matrixresult.x20 = matrixconv.x20;  matrixresult.x21 = matrixconv.x21;
        matrixresult.x22 = matrixconv.x22;  matrixresult.x23 = matrixconv.x23;
        matrixresult.x30 = matrixconv.x30;  matrixresult.x31 = matrixconv.x31;
        matrixresult.x32 = matrixconv.x32;  matrixresult.x33 = matrixconv.x33;

	}
	else if((incs == 0) && (outcs == 1)) //rgb to yuv
	{
		ptmatrix = (__scal_matrix4x4 *)((__u32)image_enhance_tab + (in_csc_mode<<7));
		iDE_SCAL_Matrix_Mul(matrixEn, *ptmatrix, &matrixconv);
        matrixresult.x00 = matrixconv.x01;  matrixresult.x01 = matrixconv.x00;
        matrixresult.x02 = matrixconv.x02;  matrixresult.x03 = matrixconv.x03;
        matrixresult.x10 = matrixconv.x11;  matrixresult.x11 = matrixconv.x10;
        matrixresult.x12 = matrixconv.x12;  matrixresult.x13 = matrixconv.x13;
        matrixresult.x20 = matrixconv.x21;  matrixresult.x21 = matrixconv.x20;
        matrixresult.x22 = matrixconv.x22;  matrixresult.x23 = matrixconv.x23;
        matrixresult.x30 = matrixconv.x31;  matrixresult.x31 = matrixconv.x30;
        matrixresult.x32 = matrixconv.x32;  matrixresult.x33 = matrixconv.x33;
	}
	else  //yuv to yuv
	{
		matrixresult = matrixEn;
	}

    //data bit convert, 1 bit  sign, 2 bit integer, 10 bits fractrional for coefficient; 1 bit sign,9 bit integer, 4 bit fractional for constant
    //range limited
    iDE_SCAL_Csc_Lmt(&matrixresult.x00, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x01, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x02, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x03, -8191, 8191, 6, 16383);
    iDE_SCAL_Csc_Lmt(&matrixresult.x10, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x11, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x12, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x13, -8191, 8191, 6, 16383);
    iDE_SCAL_Csc_Lmt(&matrixresult.x20, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x21, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x22, -4095, 4095, 0, 8191);
    iDE_SCAL_Csc_Lmt(&matrixresult.x23, -8191, 8191, 6, 16383);

    //write csc register
    pt = &(matrixresult.x00);
    for(i=0;i<12;i++)
    {
        scal_dev[sel]->csc_coef[i]= *(pt + i);
    }
    scal_dev[sel]->bypass.csc_bypass_en = 0;

	return 0;
}

__s32 DE_SCAL_EnableINT(__u8 sel,__u32 irqsrc)
{
	if(irqsrc & DE_WB_END_IE)
	{
		scal_dev[sel]->int_en.wb_en = 1;
	}

	return 0;
}

__s32 DE_SCAL_DisableINT(__u8 sel, __u32 irqsrc)
{
	if(irqsrc & DE_WB_END_IE)
	{
		scal_dev[sel]->int_en.wb_en = 0;
	}
	return 0;
}

__u32 DE_SCAL_QueryINT(__u8 sel)
{
	__u32 ret = 0;

	ret |= (scal_dev[sel]->int_status.wb_sts ==1)?DE_WB_END_IE:0;

	return ret;
}

__u32 DE_SCAL_ClearINT(__u8 sel,__u32 irqsrc)
{
	if(irqsrc & DE_WB_END_IE)
	{
		scal_dev[sel]->int_status.wb_sts = 1;
	}
	return 0;
}
