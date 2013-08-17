/* ibmmpeg2.h - IBM MPEGCD21 definitions */

#ifndef __IBM_MPEG2__
#define __IBM_MPEG2__

/* Define all MPEG Decoder registers */
/* Chip Control and Status */
#define IBM_MP2_CHIP_CONTROL	0x200*2
#define IBM_MP2_CHIP_MODE	0x201*2
/* Timer Control and Status */
#define IBM_MP2_SYNC_STC2	0x202*2
#define IBM_MP2_SYNC_STC1	0x203*2
#define IBM_MP2_SYNC_STC0	0x204*2
#define IBM_MP2_SYNC_PTS2	0x205*2
#define IBM_MP2_SYNC_PTS1	0x206*2
#define IBM_MP2_SYNC_PTS0	0x207*2
/* Video FIFO Control */
#define IBM_MP2_FIFO		0x208*2
#define IBM_MP2_FIFOW		0x100*2
#define IBM_MP2_FIFO_STAT	0x209*2
#define IBM_MP2_RB_THRESHOLD	0x22b*2
/* Command buffer */
#define IBM_MP2_COMMAND		0x20a*2
#define IBM_MP2_CMD_DATA	0x20b*2
#define IBM_MP2_CMD_STAT	0x20c*2
#define IBM_MP2_CMD_ADDR	0x20d*2
/* Internal Processor Control and Status */
#define IBM_MP2_PROC_IADDR	0x20e*2
#define IBM_MP2_PROC_IDATA	0x20f*2
#define IBM_MP2_WR_PROT		0x235*2
/* DRAM Access */
#define IBM_MP2_DRAM_ADDR	0x210*2
#define IBM_MP2_DRAM_DATA	0x212*2
#define IBM_MP2_DRAM_CMD_STAT	0x213*2
#define IBM_MP2_BLOCK_SIZE	0x23b*2
#define IBM_MP2_SRC_ADDR	0x23c*2
/* Onscreen Display */
#define IBM_MP2_OSD_ADDR	0x214*2
#define IBM_MP2_OSD_DATA	0x215*2
#define IBM_MP2_OSD_MODE	0x217*2
#define IBM_MP2_OSD_LINK_ADDR	0x229*2
#define IBM_MP2_OSD_SIZE	0x22a*2
/* Interrupt Control */
#define IBM_MP2_HOST_INT	0x218*2
#define IBM_MP2_MASK0		0x219*2
#define IBM_MP2_HOST_INT1	0x23e*2
#define IBM_MP2_MASK1		0x23f*2
/* Audio Control */
#define IBM_MP2_AUD_IADDR	0x21a*2
#define IBM_MP2_AUD_IDATA	0x21b*2
#define IBM_MP2_AUD_FIFO	0x21c*2
#define IBM_MP2_AUD_FIFOW	0x101*2
#define IBM_MP2_AUD_CTL		0x21d*2
#define IBM_MP2_BEEP_CTL	0x21e*2
#define IBM_MP2_FRNT_ATTEN	0x22d*2
/* Display Control */
#define IBM_MP2_DISP_MODE	0x220*2
#define IBM_MP2_DISP_DLY	0x221*2
#define IBM_MP2_VBI_CTL		0x222*2
#define IBM_MP2_DISP_LBOR	0x223*2
#define IBM_MP2_DISP_TBOR	0x224*2
/* Polarity Control */
#define IBM_MP2_INFC_CTL	0x22c*2

/* control commands */
#define IBM_MP2_PLAY		0
#define IBM_MP2_PAUSE		1
#define IBM_MP2_SINGLE_FRAME	2
#define IBM_MP2_FAST_FORWARD	3
#define IBM_MP2_SLOW_MOTION	4
#define IBM_MP2_IMED_NORM_PLAY	5
#define IBM_MP2_RESET_WINDOW	6
#define IBM_MP2_FREEZE_FRAME	7
#define IBM_MP2_RESET_VID_RATE	8
#define IBM_MP2_CONFIG_DECODER	9
#define IBM_MP2_CHANNEL_SWITCH	10
#define IBM_MP2_RESET_AUD_RATE	11
#define IBM_MP2_PRE_OP_CHN_SW	12
#define IBM_MP2_SET_STILL_MODE	14

/* Define Xilinx FPGA Internal Registers */

/* general control register 0 */
#define XILINX_CTL0		0x600
/* genlock delay resister 1 */
#define XILINX_GLDELAY		0x602
/* send 16 bits to CS3310 port */
#define XILINX_CS3310		0x604
/* send 16 bits to CS3310 and complete */
#define XILINX_CS3310_CMPLT	0x60c
/* pulse width modulator control */
#define XILINX_PWM		0x606

#endif
