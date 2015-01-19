/*
 * Amlogic M1 & M2
 * frame buffer driver  -------bt656 & 601 input
 * Copyright (C) 2010 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <asm/delay.h>
#include <asm/atomic.h>
#include <linux/module.h>
#include <linux/fs.h>

#include <mach/am_regs.h>
#include <mach/mod_gate.h>

#include "../tvin_global.h"
#include "../tvin_format_table.h"
#include "../tvin_frontend.h"
#include "bt656_601_in.h"

#define DEV_NAME  "amvdec_656in"
#define DRV_NAME  "amvdec_656in"
#define CLS_NAME  "amvdec_656in"
#define MOD_NAME  "amvdec_656in"

//#define HANDLE_BT656IN_IRQ

#define BT656_MAX_DEVS             1
//#define BT656IN_ANCI_DATA_SIZE        0x4000

#define BT656_VER "2013/11/12"

/* Per-device (per-bank) structure */

//static struct am656in_dev_t am656in_dev_;
static dev_t am656in_devno;
static struct class *am656in_clsp;
//static struct am656in_dev_s *am656in_devp[BT656_MAX_DEVS];

#ifdef HANDLE_BT656IN_IRQ
static const char bt656in_dec_id[] = "bt656in-dev";
#endif

static void init_656in_dec_parameter(struct am656in_dev_s *devp)
{
	enum tvin_sig_fmt_e fmt;
        const struct tvin_format_s * fmt_info_p;
        fmt = devp->para.fmt;
        fmt_info_p = tvin_get_fmt_info(fmt);

    if(!fmt_info_p) {
        printk("[bt656..]%s:invaild fmt %d.\n",__func__, fmt);
        return;
    }

	if(fmt < TVIN_SIG_FMT_MAX)
	{
		devp->para.v_active    = fmt_info_p->v_active;
		devp->para.h_active    = fmt_info_p->h_active;
                devp->para.hsync_phase = 0;
                devp->para.vsync_phase = 0;
                devp->para.hs_bp       = 0;
                devp->para.vs_bp       = 0;
	}
}

static void reset_bt656in_module(void)
{
	int temp_data;

	temp_data = RD(BT_CTRL);
	temp_data &= ~( 1 << BT_EN_BIT );
	WR(BT_CTRL, temp_data); //disable BT656 input

	// reset BT656in module.
	temp_data = RD(BT_CTRL);
	temp_data |= ( 1 << BT_SOFT_RESET );
	WR(BT_CTRL, temp_data);

	temp_data = RD(BT_CTRL);
	temp_data &= ~( 1 << BT_SOFT_RESET );
	WR(BT_CTRL, temp_data);

}

/*
   NTSC or PAL input(interlace mode): CLOCK + D0~D7(with SAV + EAV )
 */
static void reinit_bt656in_dec(struct am656in_dev_s *devp)
{
	reset_bt656in_module();

	WR(BT_FIELDSADR, (4 << 16) | 4);    // field 0/1 start lcnt: default value
	// configuration the BT PORT control
	// For standaREAD_CBUS_REG bt656 in stream, there's no HSYNC VSYNC pins.
	// So we don't need to configure the port.
	WR(BT_PORT_CTRL, 1 << BT_D8B);  // data itself is 8 bits.

	WR(BT_SWAP_CTRL,    ( 4 << 0 ) |        //POS_Y1_IN
			( 5 << 4 ) |        //POS_Cr0_IN
			( 6 << 8 ) |        //POS_Y0_IN
			( 7 << 12 ));       //POS_CB0_IN

	WR(BT_LINECTRL , 0)  ;
	//there is no use anci in m2
	// ANCI is the field blanking data, like close caption. If it connected to digital camara interface, the jpeg bitstream also use this ANCI FIFO.
	//WR(BT_ANCISADR, devp->mem_start);
	//WR(BT_ANCIEADR, 0); //devp->mem_start + devp->mem_size);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8

#else
	WR(BT_AFIFO_CTRL,   (1 <<31) |     // load start and end address to afifo.
			(1 << 6) |     // fill _en;
			(1 << 3)) ;     // urgent
#endif

	WR(BT_INT_CTRL ,   // (1 << 5) |    //ancififo done int.
			//                      (1 << 4) |    //SOF interrupt enable.
			//                      (1 << 3) |      //EOF interrupt enable.
			(1 << 1)); // |      //input overflow interrupt enable.
	//                      (1 << 0));      //bt656 controller error interrupt enable.

	WR(BT_ERR_CNT, (626 << 16) | (1760));

	if(devp->para.fmt== TVIN_SIG_FMT_BT656IN_576I_50HZ) //input is PAL
	{
		WR(BT_VBIEND,   22 | (22 << 16));       //field 0/1 VBI last line number
		WR(BT_VIDEOSTART,   23 | (23 << 16));   //Line number of the first video start line in field 0/1.
		WR(BT_VIDEOEND ,    312 |          //  Line number of the last video line in field 1. added video end for avoid overflow.
				(312 <<16));                    // Line number of the last video line in field 0
		WR(BT_CTRL ,    (0 << BT_UPDATE_ST_SEL) |  //Update bt656 status register when end of frame.
				(1 << BT_COLOR_REPEAT) | //Repeated the color data when do 4:2:2 -> 4:4:4 data transfer.
				(1 << BT_AUTO_FMT ) |           //use haREAD_CBUS_REGware to check the PAL/NTSC format input format if it's standaREAD_CBUS_REG BT656 input format.
				(1 << BT_MODE_BIT     ) | // BT656 standaREAD_CBUS_REG interface.
				(1 << BT_EN_BIT       ) |    // enable BT moduale.
				(1 << BT_REF_MODE_BIT ) |    // timing reference is from bit stream.
				(1 << BT_CLK27_SEL_BIT) |    // use external xclk27.
				(1 << BT_XCLK27_EN_BIT)) ;    // xclk27 is input.
		WR(VDIN_WR_V_START_END, 287 |     //v end
				(0 << 16) );   // v start

	}
	else //if(am656in_dec_info.para.fmt  == TVIN_SIG_FMT_BT656IN_480I) //input is PAL   //input is NTSC
	{
		WR(BT_VBIEND,   21 | (21 << 16));       //field 0/1 VBI last line number
		WR(BT_VIDEOSTART,   18 | (18 << 16));   //Line number of the first video start line in field 0/1.
		WR(BT_VIDEOEND ,    257 |          //  Line number of the last video line in field 1. added video end for avoid overflow.
				(257 <<16));                    // Line number of the last video line in field 0
		WR(BT_CTRL ,    (0 << BT_UPDATE_ST_SEL) |  //Update bt656 status register when end of frame.
				(1 << BT_COLOR_REPEAT) | //Repeated the color data when do 4:2:2 -> 4:4:4 data transfer.
				(1 << BT_AUTO_FMT ) |       //use haREAD_CBUS_REGware to check the PAL/NTSC format input format if it's standaREAD_CBUS_REG BT656 input format.
				(1 << BT_MODE_BIT     ) | // BT656 standaREAD_CBUS_REG interface.
				(1 << BT_EN_BIT       ) |    // enable BT moduale.
				(1 << BT_REF_MODE_BIT ) |    // timing reference is from bit stream.
				(1 << BT_CLK27_SEL_BIT) |    // use external xclk27.
				(1 << BT_XCLK27_EN_BIT) |       // xclk27 is input.
				(1 << BT_FMT_MODE_BIT));   //input format is NTSC
		WR(VDIN_WR_V_START_END, 239 |     //v end
				(0 << 16) );   // v start

	}

	return;
}

//NTSC or PAL input(interlace mode): CLOCK + D0~D7 + HSYNC + VSYNC + FID
static void reinit_bt601in_dec(struct am656in_dev_s *devp)
{
	reset_bt656in_module();

	WR(BT_PORT_CTRL,    (0 << BT_IDQ_EN )   |     // use external idq pin.
			(1 << BT_IDQ_PHASE )   |
			( 1 << BT_FID_HSVS ) |         // FID came from HS VS.
			( 1 << BT_HSYNC_PHASE ) |
			(1 << BT_D8B )     |
			(4 << BT_FID_DELAY ) |
			(5 << BT_VSYNC_DELAY) |
			(5 << BT_HSYNC_DELAY));

	WR(BT_601_CTRL2 , ( 10 << 16));     // FID field check done point.

	WR(BT_SWAP_CTRL,    ( 4 << 0 ) | // suppose the input bitstream format is Cb0 Y0 Cr0 Y1.
			( 5 << 4 ) |
			( 6 << 8 ) |
			( 7 << 13 ) );

	WR(BT_LINECTRL , ( 1 << 31 ) |   //software line ctrl enable.
			(1644 << 16 ) |    //1440 + 204
			220 )  ;

	// ANCI is the field blanking data, like close caption. If it connected to digital camara interface, the jpeg bitstream also use this ANCI FIFO.
	//WR(BT_ANCISADR, devp->mem_start);
	//WR(BT_ANCIEADR, 0);//devp->mem_start + devp->mem_size);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8

#else
	WR(BT_AFIFO_CTRL,   (1 <<31) |     // load start and end address to afifo.
			(1 << 6) |     // fill _en;
			(1 << 3)) ;     // urgent
#endif
	WR(BT_INT_CTRL ,   // (1 << 5) |    //ancififo done int.
			//                      (1 << 4) |    //SOF interrupt enable.
			//                      (1 << 3) |      //EOF interrupt enable.
			(1 << 1)); // |      //input overflow interrupt enable.
	//                      (1 << 0));      //bt656 controller error interrupt enable.
	WR(BT_ERR_CNT, (626 << 16) | (2000));
	//otherwise there is always error flag,
	//because the camera input use HREF ont HSYNC,
	//there are some lines without HREF sometime
	WR(BT_FIELDSADR, (1 << 16) | 1);    // field 0/1 start lcnt

	if(devp->para.fmt == TVIN_SIG_FMT_BT601IN_576I_50HZ) //input is PAL
	{
		WR(BT_VBIEND, 22 | (22 << 16));     //field 0/1 VBI last line number
		WR(BT_VIDEOSTART, 23 | (23 << 16)); //Line number of the first video start line in field 0/1.
		WR(BT_VIDEOEND , 312 |          //  Line number of the last video line in field 1. added video end for avoid overflow.
				(312 <<16));                    // Line number of the last video line in field 0
		WR(BT_CTRL ,    (0 << BT_MODE_BIT     ) |    // BT656 standaREAD_CBUS_REG interface.
				(1 << BT_AUTO_FMT )     |
				(1 << BT_EN_BIT       ) |    // enable BT moduale.
				(0 << BT_REF_MODE_BIT ) |    // timing reference is from bit stream.
				(0 << BT_FMT_MODE_BIT ) |     //PAL
				(1 << BT_SLICE_MODE_BIT )|    // no ancillay flag.
				(0 << BT_FID_EN_BIT )   |     // use external fid pin.
				(1 << BT_CLK27_SEL_BIT) |  // use external xclk27.
				(1 << BT_XCLK27_EN_BIT) );   // xclk27 is input.
		WR(VDIN_WR_V_START_END, 287 |     //v end
				(0 << 16) );   // v start
	}
	else //if(am656in_dec_info.para.fmt == TVIN_SIG_FMT_BT601IN_480I)   //input is NTSC
	{
		WR(BT_VBIEND, 21 | (21 << 16));     //field 0/1 VBI last line number
		WR(BT_VIDEOSTART, 18 | (18 << 16)); //Line number of the first video start line in field 0/1.
		WR(BT_VIDEOEND , 257 |          //  Line number of the last video line in field 1. added video end for avoid overflow.
				(257 <<16));        // Line number of the last video line in field 0
		WR(BT_CTRL ,(0 << BT_MODE_BIT     ) |    // BT656 standaREAD_CBUS_REG interface.
				(1 << BT_AUTO_FMT )     |
				(1 << BT_EN_BIT       ) |    // enablem656in_star BT moduale.
				(0 << BT_REF_MODE_BIT ) |    // timing reference is from bit stream.
				(1 << BT_FMT_MODE_BIT ) |     // NTSC
				(1 << BT_SLICE_MODE_BIT )|    // no ancillay flag.
				(0 << BT_FID_EN_BIT )   |     // use external fid pin.
				(1 << BT_CLK27_SEL_BIT) |  // use external xclk27.
				(1 << BT_XCLK27_EN_BIT) );   // xclk27 is input.
		WR(VDIN_WR_V_START_END, 239 |     //v end
				(0 << 16) );   // v start

	}

	return;
}

//CAMERA input(progressive mode): CLOCK + D0~D7 + HREF + VSYNC
static void reinit_camera_dec(struct am656in_dev_s *devp)
{
	//reset_bt656in_module();
	unsigned int temp_data;
	unsigned char hsync_enable = devp->para.hsync_phase;
	unsigned char vsync_enable = devp->para.vsync_phase;
        unsigned short hs_bp       = devp->para.hs_bp;
        unsigned short vs_bp       = devp->para.vs_bp;

        #ifdef MESON_CPU_TYPE_MESON8B
	//top reset for bt656
        WRITE_CBUS_REG_BITS(RESET1_REGISTER,1,5,1);
        WRITE_CBUS_REG_BITS(RESET1_REGISTER,0,5,1);
        #endif
        //disable 656,reset
	WR(BT_CTRL, 1<<31);

	/*WR(BT_VIDEOSTART, 1 | (1 << 16));   //Line number of the first video start line in field 0/1.there is a blank
	  WR(BT_VIDEOEND , (am656in_dec_info.active_line )|          //  Line number of the last video line in field 1. added video end for avoid overflow.
	  ((am656in_dec_info.active_line ) << 16));      */             // Line number of the last video line in field 0
	WR(BT_PORT_CTRL, (0 << BT_IDQ_EN )   |     // use external idq pin.
			(0 << BT_IDQ_PHASE )   |
			(0 << BT_FID_HSVS )    |         // FID came from HS VS.
			(vsync_enable << BT_VSYNC_PHASE) |
			(hsync_enable << BT_HSYNC_PHASE) |
			(0 << BT_D8B )         |
			(4 << BT_FID_DELAY )   |
			(0 << BT_VSYNC_DELAY)  |
			(2 << BT_HSYNC_DELAY)

		      );
	//WRITE_CBUS_REG(BT_PORT_CTRL,0x421001); 

	WR(BT_601_CTRL2 , ( 10 << 16));     // FID field check done point.

	WR(BT_SWAP_CTRL,
			( 7 << 0 ) |        //POS_Cb0_IN
			( 4 << 4 ) |        //POS_Y0_IN
			( 5 << 8 ) |        //POS_Cr0_IN
			( 6 << 12 ));       //POS_Y1_IN

	WR(BT_LINECTRL , ( 1<< 31) |   //software line ctrl enable.
			((devp->para.h_active<< 1)<< 16 ) |    //the number of active data per line
			hs_bp);//horizontal active data start offset

	// ANCI is the field blanking data, like close caption. If it connected to digital camara interface, the jpeg bitstream also use this ANCI FIFO.
	//WR(BT_ANCISADR, devp->mem_start);
	//WR(BT_ANCIEADR, 0);//devp->mem_start + devp->mem_size);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8

#else
	WR(BT_AFIFO_CTRL,   (1 <<31) |     // load start and end address to afifo.
			(1 << 6) |     // fill _en;
			(1 << 3)) ;     // urgent
#endif
	WR(BT_INT_CTRL ,    //(1 << 5) |    //ancififo done int.
			//(1 << 4) |    //SOF interrupt enable.
			//(1 << 3) |      //EOF interrupt enable.
			(1 << 1));      //input overflow interrupt enable.
	//(1 << 0));      //bt656 controller error interrupt enable.

	WR(BT_ERR_CNT, ((2000) << 16) | (2000 * 10));   //total lines per frame and total pixel per line
	//otherwise there is always error flag,
	//because the camera input use HREF ont HSYNC,
	//there are some lines without HREF sometime

	WR(BT_FIELDSADR, (1 << 16) | 1);    // field 0/1 start lcnt

	WR(BT_VBISTART, 1 | (1 << 16));       //field 0/1 VBI last line number
	WR(BT_VBIEND,   1 | (1 << 16));       //field 0/1 VBI last line number


	WR(BT_VIDEOSTART, vs_bp | (vs_bp << 16));   //Line number of the first video start line in field 0/1.there is a blank
	WR(BT_VIDEOEND , (devp->para.v_active + vs_bp) |          //  Line number of the last video line in field 1. added video end for avoid overflow.
			 ((devp->para.v_active + vs_bp) << 16));                   // Line number of the last video line in field 0

	//enable BTR656 interface
	#if (defined CONFIG_ARCH_MESON6)
	WR(BT_CTRL , (1 << BT_EN_BIT)    // enable BT moduale.
			|(0 << BT_REF_MODE_BIT )      // timing reference is from bit stream.
			|(0 << BT_FMT_MODE_BIT )      //PAL
			|(1 << BT_SLICE_MODE_BIT )   // no ancillay flag.
			|(0 << BT_MODE_BIT)              // BT656 standard interface.
			|(1 << BT_CLOCK_ENABLE)      // enable 656 clock.
			|(0 << BT_FID_EN_BIT)            // use external fid pin.
			|(1 << BT_XCLK27_EN_BIT)     // xclk27 is input.
			|(1 << BT_PROG_MODE ) 
			|(0 << BT_AUTO_FMT)  
			|(1 << BT_CAMERA_MODE)     // enable camera mode
			|(1 << BT_656CLOCK_RESET) 
			|(1 << BT_SYSCLOCK_RESET) 
		      );
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_7,0) ;  //disable XIF function. it's shared with gpioZ_3.
	temp_data = READ_CBUS_REG(PERIPHS_PIN_MUX_9);
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_9, temp_data|(1 << 13) |    // gpioZ 11 to bt656 clk;
			(1 << 14) |    // enable gpioZ 10:3 to be bt656 dt_in
			(1 << 15) |
			(1 << 16));
	#elif (MESON_CPU_TYPE >= CONFIG_ARCH_MESON8)
	if(devp->para.isp_fe_port == TVIN_PORT_CAMERA) {		
	        temp_data = (1 << BT_EN_BIT)    // enable BT moduale.
			|(0 << BT_REF_MODE_BIT )      // timing reference is from bit stream.
			|(0 << BT_FMT_MODE_BIT )      //PAL
			|(0 << BT_SLICE_MODE_BIT )   // no ancillay flag.
			|(0 << BT_MODE_BIT)              // BT656 standard interface.
			|(1 << BT_CLOCK_ENABLE)      // enable 656 clock.
			|(0 << BT_FID_EN_BIT)            // use external fid pin.
			//|(0 << BT_XCLK27_EN_BIT)     // xclk27 is input. change to Raw_mode setting from M8
			|(0 << BT_PROG_MODE ) 
			|(0 << BT_AUTO_FMT)  
			|(0 << BT_CAMERA_MODE)     // enable camera mode
			|(1 << BT_656CLOCK_RESET) 
			|(1 << BT_SYSCLOCK_RESET) 
			|(1<<9)					//enable raw data output
			|(1<<27)				//enable raw data to isp
			|(0<<28)				//enable csi2 pin
		    ;
	} else {	
	        temp_data = (1 << BT_EN_BIT)    // enable BT moduale.
			|(0 << BT_REF_MODE_BIT )      // timing reference is from bit stream.
			|(0 << BT_FMT_MODE_BIT )      //PAL
			|(1 << BT_SLICE_MODE_BIT )   // no ancillay flag.
			|(0 << BT_MODE_BIT)              // BT656 standard interface.
			|(1 << BT_CLOCK_ENABLE)      // enable 656 clock.
			|(0 << BT_FID_EN_BIT)            // use external fid pin.
			//|(1 << BT_XCLK27_EN_BIT)     // xclk27 is input. change to Raw_mode setting from M8
			|(1 << BT_PROG_MODE ) 
			|(0 << BT_AUTO_FMT)  
			|(1 << BT_CAMERA_MODE)     // enable camera mode
			|(1 << BT_656CLOCK_RESET) 
			|(1 << BT_SYSCLOCK_RESET) 
			;
	}
	if(devp->para.bt_path == BT_PATH_GPIO) {
		temp_data &= (~(1<<28));
		WR(BT_CTRL,temp_data);
	} else if(devp->para.bt_path == BT_PATH_CSI2){
		temp_data |= (1<<28);
		WR(BT_CTRL,temp_data);
		//power on mipi csi phy
		WRITE_CBUS_REG(HHI_CSI_PHY_CNTL0,0xfdc1ff81);
		WRITE_CBUS_REG(HHI_CSI_PHY_CNTL1,0x3fffff);
	        temp_data = READ_CBUS_REG(HHI_CSI_PHY_CNTL2);
                temp_data &= 0x7ff00000;
                temp_data |= 0x80000fc0;
                WRITE_CBUS_REG(HHI_CSI_PHY_CNTL2,temp_data);
	}
		
	#endif

	return;
}

static void start_amvdec_656_601_camera_in(struct am656in_dev_s *devp)
{
	enum tvin_port_e port =  devp->para.port;
        if(devp->dec_status & TVIN_AM656_RUNING){
                printk("[bt656..] %s bt656 have started alreadly.\n",__func__);
                return;
        }
        devp->dec_status = TVIN_AM656_RUNING; 
	//NTSC or PAL input(interlace mode): D0~D7(with SAV + EAV )
	if(port == TVIN_PORT_BT656){
		devp->para.fmt=TVIN_SIG_FMT_BT656IN_576I_50HZ;
		init_656in_dec_parameter(devp);
		reinit_bt656in_dec(devp);
		//reset_656in_dec_parameter();
		devp->dec_status = TVIN_AM656_RUNING;
	}
	else if(port == TVIN_PORT_BT601){
		devp->para.fmt=TVIN_SIG_FMT_BT601IN_576I_50HZ;
		init_656in_dec_parameter(devp);
		reinit_bt601in_dec(devp);
		devp->dec_status = TVIN_AM656_RUNING;

	}
	else if(port == TVIN_PORT_CAMERA){
		init_656in_dec_parameter(devp);
		reinit_camera_dec(devp);
		devp->dec_status = TVIN_AM656_RUNING;
	}
	else
	{
		devp->para.fmt  = TVIN_SIG_FMT_NULL;
		devp->para.port = TVIN_PORT_NULL;
		printk("[bt656..]%s: input is not selected, please try again. \n",__func__);
		return;
	}
    printk("[bt656(%s)]: %s input port: %s fmt: %s.\n",BT656_VER,__func__,
            tvin_port_str(devp->para.port),tvin_sig_fmt_str(devp->para.fmt));
	
	return;
}

static void stop_amvdec_656_601_camera_in(struct am656in_dev_s *devp)
{
	if(devp->dec_status & TVIN_AM656_RUNING){
		reset_bt656in_module();
		devp->dec_status = TVIN_AM656_STOP;
	}
	else{
		printk("[bt656..] %s device is not started yet. \n",__func__);
	}
	return;
}

/*
   return true when need skip frame otherwise return false
 */
static bool am656_check_skip_frame(struct tvin_frontend_s * fe)
{
	struct am656in_dev_s * devp =  container_of(fe, am656in_dev_t, frontend);
	if(devp->skip_vdin_frame_count > 0)
	{
		devp->skip_vdin_frame_count--;
		return true;
	}
	else
		return false;
}
int am656in_support(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
	if((port < TVIN_PORT_BT601) ||(port > TVIN_PORT_CAMERA))
		return -1;
	else
		return 0;
}

static int am656in_open(struct inode *node, struct file *file)
{
	am656in_dev_t *bt656_in_devp;
	/* Get the per-device structure that contains this cdev */
	bt656_in_devp = container_of(node->i_cdev, am656in_dev_t, cdev);
	file->private_data = bt656_in_devp;
	return 0;

}
static int am656in_release(struct inode *node, struct file *file)
{    
	file->private_data = NULL;
	return 0;
}

static struct file_operations am656in_fops = {
	.owner    = THIS_MODULE,
	.open     = am656in_open,
	.release  = am656in_release,
};
/*called by vdin && sever for v4l2 framework*/

void am656in_start(struct tvin_frontend_s *fe, enum tvin_sig_fmt_e fmt)
{
	struct am656in_dev_s *am656_devp;
	am656_devp = container_of(fe, am656in_dev_t, frontend);
	start_amvdec_656_601_camera_in(am656_devp);

}
static void am656in_stop(struct tvin_frontend_s * fe, enum tvin_port_e port)
{
	struct am656in_dev_s *devp = container_of(fe, am656in_dev_t, frontend);
	if((port < TVIN_PORT_BT656)||(port > TVIN_PORT_CAMERA)){
		printk("[bt656..]%s:invaild port %d.\n",__func__, port);
		return;
	}
	stop_amvdec_656_601_camera_in(devp);
	pr_info("[bt656..] %s stop device stop ok. \n", __func__);
}
static void am656in_get_sig_propery(struct tvin_frontend_s *fe, struct tvin_sig_property_s *prop)
{
	struct am656in_dev_s *devp = container_of(fe, am656in_dev_t, frontend);
	prop->color_format = devp->para.cfmt;
	prop->dest_cfmt    = devp->para.dfmt;
	prop->decimation_ratio = 0;
}

/*as use the spin_lock,
 *1--there is no sleep,
 *2--it is better to shorter the time,
 */
int am656in_isr(struct tvin_frontend_s *fe, unsigned int hcnt)
{
	unsigned int ccir656_status = 0;
	struct am656in_dev_s *devp = container_of(fe, am656in_dev_t, frontend);
	ccir656_status = RD(BT_STATUS);
	if(ccir656_status & 0xf0)   //AFIFO OVERFLOW
		devp->overflow_cnt++;
	if(devp->overflow_cnt > 5)
	{
		devp->overflow_cnt = 0;
		if(devp->para.port == TVIN_PORT_BT656)  //NTSC or PAL input(interlace mode): D0~D7(with SAV + EAV )
			reinit_bt656in_dec(devp);
		else if(devp->para.port == TVIN_PORT_BT601)
			reinit_bt601in_dec(devp);
		else //if(am656in_dec_info.para.port == TVIN_PORT_CAMERA)
			reinit_camera_dec(devp);
		WR(BT_STATUS, ccir656_status | (1 << 9));   //WRITE_CBUS_REGite 1 to clean the SOF interrupt bit
		printk("[bt656..] %s bt656in fifo overflow. \n",__func__);
	}
	return 0;
}
/*
*power on 656 module&init the parameters,such as color fmt...,will be used by vdin
*/
static int am656in_feopen(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
	struct am656in_dev_s *am656_devp = container_of(fe, am656in_dev_t, frontend);
	struct vdin_parm_s *parm = fe->private_data;
	if((port < TVIN_PORT_BT656)||(port > TVIN_PORT_CAMERA)){
		printk("[bt656..]%s:invaild port %d.\n",__func__, port);
		return -1;
	}
	if( TVIN_PORT_CAMERA == port ){
		am656_devp->skip_vdin_frame_count = parm->skip_count;
	}
	/*copy the param from vdin to bt656*/
	if(!memcpy(&am656_devp->para, parm, sizeof(vdin_parm_t))){
		printk("[bt656..]%s memcpy error.\n",__func__);
		return -1;
	}
	/*avoidint the param port is't equal with port*/
	am656_devp->para.port = port;
        printk("[bt656..] %s color format %s,hsync phase %u,vsync phase %u,"\
                "frame rate %u,hs_bp %u,vs_bp %u.\n", __func__,
                tvin_color_fmt_str(parm->cfmt),parm->hsync_phase,
                parm->vsync_phase,parm->frame_rate,parm->hs_bp,parm->vs_bp);
	#ifdef CONFIG_ARCH_MESON6
	switch_mod_gate_by_name("bt656", 1);
	#endif
	return 0;

}
/*
*power off the 656 module,clear the parameters
*/
static void am656in_feclose(struct tvin_frontend_s *fe)
{

	struct am656in_dev_s *devp = container_of(fe, am656in_dev_t, frontend);
        enum tvin_port_e port = devp->para.port;
	if((port < TVIN_PORT_BT656)||(port > TVIN_PORT_CAMERA)){
		printk("[bt656..]%s:invaild port %d.\n",__func__, port);
		return;
	}
	#ifdef CONFIG_ARCH_MESON6
	switch_mod_gate_by_name("bt656", 0);
	#endif
	memset(&devp->para, 0, sizeof(vdin_parm_t));
}
static struct tvin_state_machine_ops_s am656_machine_ops = {
	.nosig               = NULL,
	.fmt_changed         = NULL,
	.get_fmt             = NULL,
	.fmt_config          = NULL,
	.adc_cal             = NULL,
	.pll_lock            = NULL,
	.get_sig_propery     = am656in_get_sig_propery,
	.vga_set_param       = NULL,
	.vga_get_param       = NULL,
	.check_frame_skip    = am656_check_skip_frame,
};
static struct tvin_decoder_ops_s am656_decoder_ops_s = {
	.support                = am656in_support,
	.open                   = am656in_feopen,
	.start                  = am656in_start,
	.stop                   = am656in_stop,
	.close                  = am656in_feclose,
	.decode_isr             = am656in_isr,
};

static int bt656_add_cdev(struct cdev *cdevp,struct file_operations *fops,int minor)
{
	int ret;
	dev_t devno=MKDEV(MAJOR(am656in_devno),minor);
	cdev_init(cdevp,fops);
	cdevp->owner=THIS_MODULE;
	ret=cdev_add(cdevp,devno,1);
	return ret;
}


static struct device *bt656_create_device(struct device *parent,int minor)
{
	dev_t devno = MKDEV(MAJOR(am656in_devno),minor);
	return  device_create(am656in_clsp,parent,devno,NULL,"%s%d",
			DEV_NAME,minor);
}

static void bt656_delete_device(int minor)
{
	dev_t devno =MKDEV(MAJOR(am656in_devno),minor);
	device_destroy(am656in_clsp,devno);
}

static int amvdec_656in_probe(struct platform_device *pdev)
{
	int ret;
	struct am656in_dev_s *devp;
	//struct resource *res;
	ret = 0;

	//malloc dev
	devp = kmalloc(sizeof(struct am656in_dev_s),GFP_KERNEL);
	if(!devp){
		pr_err("%s: failed to allocate memory\n", __func__);
		goto fail_kmalloc_dev;
	}
	memset(devp,0,sizeof(struct am656in_dev_s));

//	am656in_devp[pdev->id] = devp;

	//create cdev and register with sysfs
	ret = bt656_add_cdev(&devp->cdev, &am656in_fops, 0);
	if (ret) {
		pr_err("%s: failed to add cdev\n", __func__);
		goto fail_add_cdev;
	}
	devp->dev = bt656_create_device(&pdev->dev, 0);
	if (IS_ERR(devp->dev)) {
		pr_err("%s: failed to create device\n", __func__);
		ret = PTR_ERR(devp->dev);
		goto fail_create_device;
	}
	/* get device memory */
	//res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	//if (!res) {
	//	pr_err("%s: can't get memory resource........................................\n", __func__);
	//	ret = -EFAULT;
	//	goto fail_get_resource_mem;
	//} else {
	//    devp->mem_start = res->start;
	//    devp->mem_size = res->end - res->start + 1;
	//    pr_info("%s: mem_start: 0x%x, mem_size: 0x%x ............................... \n", __func__,
	//		    devp->mem_start,
	//		    devp->mem_size);
    //}
    
    
	/*register frontend */
    sprintf(devp->frontend.name, "%s", DEV_NAME);
	//tvin_frontend_init(&devp->frontend, &am656_decoder_ops_s, &am656_machine_ops, pdev->id);
    if(!tvin_frontend_init(&devp->frontend,&am656_decoder_ops_s,&am656_machine_ops, pdev->id)) {
        if(tvin_reg_frontend(&devp->frontend))
            printk(" %s register frontend error \n",__func__);
    }        

	/*set pinmux for ITU601 A and ITU601 B*/
	/* set drvdata */
	dev_set_drvdata(devp->dev, devp);
	platform_set_drvdata(pdev, devp);
	printk("amvdec_656in probe ok.\n");
	return ret;
//fail_get_resource_mem:
fail_create_device:
	cdev_del(&devp->cdev);
fail_add_cdev:
	kfree(devp);
fail_kmalloc_dev:
	return ret;

}

static int amvdec_656in_remove(struct platform_device *pdev)
{	
	struct am656in_dev_s *devp;
	devp = platform_get_drvdata(pdev);

	tvin_unreg_frontend(&devp->frontend);
	bt656_delete_device(pdev->id);
	cdev_del(&devp->cdev);
	kfree((const void *)devp);
	/* free drvdata */
	dev_set_drvdata(devp->dev, NULL);
	platform_set_drvdata(pdev, NULL);       
	return 0;
}
//#ifdef CONFIG_OF
//static const struct of_device_id bt656_dt_match[]={
//        {       .compatible = "amlogic,amvdec_656in",   },
//        {},
//};
//#else
//#define bt656_dt_match NULL
//#endif
static struct platform_driver amvdec_656in_driver = {
	.probe      = amvdec_656in_probe,
	.remove     = amvdec_656in_remove,
	.driver     = {
		.name   = DRV_NAME,
		//.of_match_table = bt656_dt_match,
	}
};

static int __init amvdec_656in_init_module(void)
{       
        int ret = 0;
        struct platform_device *pdev;
        printk("amvdec_656in module: init.\n");
        ret=alloc_chrdev_region(&am656in_devno, 0, BT656_MAX_DEVS, DEV_NAME);
        if(ret<0){
                printk("%s:failed to alloc major number\n",__func__);
                goto fail_alloc_cdev_region;
        }
        printk("%s:major %d\n",__func__,MAJOR(am656in_devno));
        am656in_clsp=class_create(THIS_MODULE,CLS_NAME);
        if(IS_ERR(am656in_clsp)){
                ret=PTR_ERR(am656in_clsp);
                printk("%s:failed to create class\n",__func__);
                goto fail_class_create;
        }
#if 1
        pdev = platform_device_alloc(DEV_NAME,0);
        if(IS_ERR(pdev)){
                printk("[bt656..]%s alloc platform device error.\n", __func__);
                goto fail_pdev_create;
        }
        if(platform_device_add(pdev)){
                printk("[bt656..]%s failed register platform device.\n", __func__);
                goto fail_pdev_register;
        }
#endif
        if (0 != platform_driver_register(&amvdec_656in_driver)){
                printk("failed to register amvdec_656in driver\n");
                goto fail_pdrv_register;
        }
                        
        return 0;        
fail_pdrv_register:
        platform_device_unregister(pdev);
fail_pdev_register:
        platform_device_del(pdev);
fail_pdev_create:
        class_destroy(am656in_clsp);
fail_class_create:
        unregister_chrdev_region(am656in_devno,BT656_MAX_DEVS);
fail_alloc_cdev_region:
        return ret;

}

static void __exit amvdec_656in_exit_module(void)
{
	printk("amvdec_656in module remove.\n");
	class_destroy(am656in_clsp);
	unregister_chrdev_region(am656in_devno, BT656_MAX_DEVS);
	platform_driver_unregister(&amvdec_656in_driver);
	return ;
}

module_init(amvdec_656in_init_module);
module_exit(amvdec_656in_exit_module);

MODULE_DESCRIPTION("AMLOGIC BT656_601 input driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("2.0.0");

