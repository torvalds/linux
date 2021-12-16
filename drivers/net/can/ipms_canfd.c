/**
  ******************************************************************************
  * @file  ipms_canfd.c
  * @author  StarFive Technology
  * @version  V1.0
  * @date  09/24/2021
  * @brief
  ******************************************************************************
  * @copy
  *
  * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 20120 Shanghai StarFive Technology Co., Ltd. </center></h2>
  */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/led.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>


#define DRIVER_NAME "ipms_canfd"

// enable debug output
static int debug_on_off ;
module_param(debug_on_off,int,0);
MODULE_VERSION("1.1");

//#define CAN_DEBUG
#ifdef CAN_DEBUG
/*ipms canfd debug information*/
#define DEV_PRINT(fmt,args...) printk(KERN_DEBUG "IPMS_CANFD:CAN DEVICE is %s " fmt,ndev->name,  ## args);
#define PDEBUG(fmt,args...) if(1){printk( KERN_DEBUG "IPMS_CANFD:" fmt,## args);}
#else
#define DEV_PRINT(fmt,args...)
#define PDEBUG(fmt,args...)
#endif


#define CANFD_MASK  0x01
#define CAN_MASK    0x00
#define TX_MAX      1

/* CAN registers set */
enum canfd_device_reg {
	CANFD_RUBF_OFFSET           =   0x00,/*Receive Buffer Registers 0x00-0x4f(not finished)*/
	CANFD_RUBF_ID_OFFSET        =   0x00,
	CANFD_RBUF_CTL_OFFSET       =   0x04,
	CANFD_RBUF_DATA_OFFSET      =   0x08,

	CANFD_TBUF_OFFSET           =   0x50,   /*Transmit Buffer Registers 0x50-0x97(not finished)*/
	CANFD_TBUF_ID_OFFSET        =   0x50,
	CANFD_TBUF_CTL_OFFSET       =   0x54,
	CANFD_TBUF_DATA_OFFSET      =   0x58,

	CANFD_TTS_OFFSET            =   0x98,   /*Transmission Time Stamp 0x98-0x9f(not finished)*/ 

	CANFD_CFG_STAT_OFFSET       =   0xa0,
	CANFD_TCMD_OFFSET           =   0xa1,
	CANFD_TCTRL_OFFSET          =   0xa2,
	CANFD_RCTRL_OFFSET          =   0xa3,
	CANFD_RTIE_OFFSET           =   0xa4,
	CANFD_RTIF_OFFSET           =   0xa5,
	CANFD_ERRINT_OFFSET         =   0xa6,
	CANFD_LIMIT_OFFSET          =   0xa7,
	CANFD_S_SEG_1_OFFSET        =   0xa8,
	CANFD_S_SEG_2_OFFSET        =   0xa9,
	CANFD_S_SJW_OFFSET          =   0xaa,
	CANFD_S_PRESC_OFFSET        =   0xab,

	CANFD_F_SEG_1_OFFSET        =   0xac,
	CANFD_F_SEG_2_OFFSET        =   0xad,
	CANFD_F_SJW_OFFSET          =   0xae,
	CANFD_F_PRESC_OFFSET        =   0xaf,

	CANFD_EALCAP_OFFSET         =   0xb0,
	CANFD_RECNT_OFFSET          =   0xb2,
	CANFD_TECNT_OFFSET          =   0xb3,   
};

enum canfd_reg_bitchange{
	CAN_FD_SET_RST_MASK         =   0x80,   /*Set Reset Bit*/   
	CAN_FD_OFF_RST_MASK         =   0x7f,   /*Reset Off Bit*/

	CAN_FD_SET_FULLCAN_MASK     =   0x10,   /*set TTTBM as 1->full TTCAN mode*/
	CAN_FD_OFF_FULLCAN_MASK     =   0xef,   /*set TTTBM as 0->separate PTB and STB mode*/

	CAN_FD_SET_FIFO_MASK        =   0x20,   /*set TSMODE as 1->FIFO mode*/
	CAN_FD_OFF_FIFO_MASK        =   0xdf,   /*set TSMODE as 0->Priority mode*/

	CAN_FD_SET_TSONE_MASK       =   0x04,   
	CAN_FD_OFF_TSONE_MASK       =   0xfb,

	CAN_FD_SET_TSALL_MASK       =   0x02,
	CAN_FD_OFF_TSALL_MASK       =   0xfd,

	CAN_FD_LBMEMOD_MASK         =   0x40,   /*set loop back mode, external*/
	CAN_FD_LBMIMOD_MASK         =   0x20,

	CAN_FD_SET_BUSOFF_MASK      =   0x01,
	CAN_FD_OFF_BUSOFF_MASK      =   0xfe,

	CAN_FD_SET_TTSEN_MASK       =   0x80,   /*set ttsen, tts update enable*/
	CAN_FD_SET_BRS_MASK         =   0x10,   /*can fd Bit Rate Switch mask*/
	CAN_FD_OFF_BRS_MASK         =   0xef,   
	CAN_FD_SET_EDL_MASK         =   0x20,   /*Extended Data Length*/
	CAN_FD_OFF_EDL_MASK         =   0xdf,

	CAN_FD_SET_DLC_MASK         =   0x0f,   
	CAN_FD_SET_TENEXT_MASK      =   0x40,
	CAN_FD_SET_IDE_MASK         =   0x80,
	CAN_FD_OFF_IDE_MASK         =   0x7f,
	CAN_FD_SET_RTR_MASK         =   0x40,
	CAN_FD_OFF_RTR_MASK         =   0xbf,

	CAN_FD_INTR_ALL_MASK        =   0xff,   /*all interrupts enable mask*/
	CAN_FD_SET_RIE_MASK         =   0x80,
	CAN_FD_OFF_RIE_MASK         =   0x7f,
	CAN_FD_SET_RFIE_MASK        =   0x20,
	CAN_FD_OFF_RFIE_MASK        =   0xdf,
	CAN_FD_SET_RAFIE_MASK       =   0x10,
	CAN_FD_OFF_RAFIE_MASK       =   0xef,
	CAN_FD_SET_EIE_MASK         =   0x02,
	CAN_FD_OFF_EIE_MASK         =   0xfd,   

	CAN_FD_TASCTIVE_MASK        =   0x02,
	CAN_FD_RASCTIVE_MASK        =   0x04,

	CAN_FD_SET_TBSEL_MASK       =   0x80,   /*message writen in STB*/
	CAN_FD_OFF_TBSEL_MASK       =   0x7f,   /*message writen in PTB*/   

	CAN_FD_SET_STBY_MASK        =   0x20,
	CAN_FD_OFF_STBY_MASK        =   0xdf,

	CAN_FD_SET_TPE_MASK         =   0x10,   /*Transmit primary enable*/

	CAN_FD_SET_TPA_MASK         =   0x08,

	CAN_FD_SET_SACK_MASK        =   0x80,
	CAN_FD_SET_RREL_MASK        =   0x10,

	CAN_FD_RSTAT_NOT_EMPTY_MASK =   0x03,

	CAN_FD_SET_RIF_MASK         =   0x80,
	CAN_FD_OFF_RIF_MASK         =   0x7f,
	CAN_FD_SET_RAFIF_MASK       =   0x10,
	CAN_FD_SET_RFIF_MASK        =   0x20,

	CAN_FD_SET_TPIF_MASK        =   0x08,   /*Transmission Primary Interrupt Flag*/
	CAN_FD_SET_TSIF_MASK        =   0x04,
	CAN_FD_SET_EIF_MASK         =   0x02,
	CAN_FD_SET_AIF_MASK         =   0x01,


	CAN_FD_SET_EWARN_MASK       =   0x80,
	CAN_FD_SET_EPASS_MASK       =   0x40,
	CAN_FD_SET_EPIE_MASK        =   0x20,
	CAN_FD_SET_EPIF_MASK        =   0x10,
	CAN_FD_SET_ALIE_MASK        =   0x08,
	CAN_FD_SET_ALIF_MASK        =   0x04,
	CAN_FD_SET_BEIE_MASK        =   0x02,
	CAN_FD_SET_BEIF_MASK        =   0x01,
	CAN_FD_OFF_EPIE_MASK        =   0xdf,
	CAN_FD_OFF_BEIE_MASK        =   0xfd,

	CAN_FD_SET_AFWL_MASK        =   0x40,
	CAN_FD_SET_EWL_MASK         =   0x0b,

	CAN_FD_SET_KOER_MASK        =   0xe0,
	CAN_FD_SET_BIT_ERROR_MASK   =   0x20,
	CAN_FD_SET_FORM_ERROR_MASK  =   0x40,
	CAN_FD_SET_STUFF_ERROR_MASK =   0x60,
	CAN_FD_SET_ACK_ERROR_MASK   =   0x80,
	CAN_FD_SET_CRC_ERROR_MASK   =   0xa0,
	CAN_FD_SET_OTH_ERROR_MASK   =   0xc0,
};

static int canfd_device_driver_bittime_configuration(struct net_device *ndev);

#define BITS_OF_BITTIMING_REG	8	/*seg1,seg2,sjw,prescaler all have 8 bits*/
#define FBITS_IN_BITTIMING_STR	32	/*in can_bittiming strucure every field has 32 bits---->u32*/
#define SEG_1_SHIFT		0
#define SEG_2_SHIFT		8
#define SJW_SHIFT		16
#define PRESC_SHIFT		24
 
#define TTSEN_8_32_SHIFT	24	/*TTSEN bit used for 32 bit register read or write*/
#define RTR_32_8_SHIFT		24

/*transmit mode*/
#define XMIT_FULL		0
#define XMIT_SEP_FIFO		1
#define XMIT_SEP_PRIO		2
#define XMIT_PTB_MODE		3

struct ipms_platform_data{
	unsigned int can_or_canfd;
};

struct IPMS_canfd_priv {
	struct can_priv can;
	unsigned int tx_head;
	unsigned int tx_tail;
	unsigned int tx_max;
	struct napi_struct napi;
	u32 (*read_reg)(const struct IPMS_canfd_priv *priv, enum canfd_device_reg reg);
	void (*write_reg)(const struct IPMS_canfd_priv *priv, enum canfd_device_reg reg, u32 val);
	struct device *dev;
	void __iomem *reg_base;
	unsigned long irq_flags;
	struct clk *bus_clk;
	struct clk *can_clk;
	unsigned int tx_mode;
	unsigned int tbsel; /*tbsel=0 choose PTB,tbsel=1 choose STB*/
	unsigned int can_or_canfd;
};

/* CAN Bittiming constants */
/*Bittiming setting from IPMS DS. */
/*CANFD*/
static struct can_bittiming_const canfd_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 2,
	.tseg1_max = 17,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 8,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1,

};

/* CAN Data Bittiming constants */
/*CAN2.0*/
//TODO: t_seg2 for CAN FD nominal is differently defined
static struct can_bittiming_const canfd_data_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 2,
	.tseg1_max = 65,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1,
};

static const struct ipms_platform_data ipms_def_fd = {
	.can_or_canfd   = CANFD_MASK,   
};

static const struct ipms_platform_data ipms_def_nonfd = {
	.can_or_canfd   = CAN_MASK, 
};

static const struct of_device_id canfd_of_match[] = {
	{ .compatible = "ipms,can", .data = &ipms_def_nonfd, },
	{ .compatible = "ipms,canfd", .data = &ipms_def_fd, },
};
	
MODULE_DEVICE_TABLE(of, canfd_of_match);

static void canfd_write_reg_le(const struct IPMS_canfd_priv *priv,enum canfd_device_reg reg,u32 val){
	iowrite32(val, priv->reg_base + reg);
}

static u32 canfd_read_reg_le(const struct IPMS_canfd_priv *priv, enum canfd_device_reg reg){
	return ioread32(priv->reg_base + reg);
}


#if 1
static inline unsigned char can_ioread8(const volatile void  *addr)
{
	void  *addr_down;
	union val{
		uint8_t val_8[4];
		uint32_t val_32;
	} val;
	uint32_t offset = 0;
	addr_down = (void  *)ALIGN_DOWN((unsigned long)addr, 4);
	offset = addr - addr_down;
	val.val_32 = ioread32(addr_down);
	return val.val_8[offset];
}

static inline void can_iowrite8(unsigned char value, volatile void  *addr)
{
	void  *addr_down;
	union val{
		uint8_t val_8[4];
		uint32_t val_32;
	} val;
	uint8_t offset = 0;

	addr_down = (void  *)ALIGN_DOWN((unsigned long)addr, 4);
	offset = addr - addr_down;
	val.val_32 = ioread32(addr_down);
	val.val_8[offset] = value;
	iowrite32(val.val_32, addr_down);
}

static void canfd_reigister_set_bit(const struct IPMS_canfd_priv *priv,enum canfd_device_reg reg,enum canfd_reg_bitchange set_mask){
	void  *addr_down;
	union val{
		u8 val_8[4];
		u32 val_32;
	} val;
	u8 offset = 0;

	addr_down = (void  *)ALIGN_DOWN((unsigned long)(priv->reg_base+reg), 4);
	offset = (priv->reg_base + reg) - addr_down;
	val.val_32 = ioread32(addr_down);
	val.val_8[offset] |= set_mask;
	iowrite32(val.val_32, addr_down);
}

static void canfd_reigister_off_bit(const struct IPMS_canfd_priv *priv,enum canfd_device_reg reg,enum canfd_reg_bitchange set_mask){
	void  *addr_down;
	union val{
		u8 val_8[4];
		u32 val_32;
	} val;
	u8 offset = 0;

	addr_down = (void  *)ALIGN_DOWN((unsigned long)(priv->reg_base+reg), 4);
	offset = (priv->reg_base + reg) - addr_down;
	val.val_32 = ioread32(addr_down);
	val.val_8[offset] &= set_mask;
	iowrite32(val.val_32, addr_down);
}
#else
static void canfd_reigister_set_bit(const struct IPMS_canfd_priv *priv,enum canfd_device_reg reg,enum canfd_reg_bitchange set_mask){
	u8 temp=0;
	temp = can_ioread8(priv->reg_base+reg);
	can_iowrite8(temp|set_mask,priv->reg_base+reg);
}

static void canfd_reigister_off_bit(const struct IPMS_canfd_priv *priv,enum canfd_device_reg reg,enum canfd_reg_bitchange set_mask){
	u8 temp=0;
	temp = can_ioread8(priv->reg_base+reg);
	can_iowrite8(temp&set_mask,priv->reg_base+reg);
}
#endif

static int canfd_device_driver_bittime_configuration(struct net_device *ndev){

	struct IPMS_canfd_priv *priv= netdev_priv(ndev);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct can_bittiming *dbt = &priv->can.data_bittiming;
	u32 reset_test,bittiming_temp,data_bittiming_temp,temp;
	
	reset_test=can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);
	if(!(reset_test & CAN_FD_SET_RST_MASK)){
		netdev_alert(ndev, "Not in reset mode, cannot set bit timing\n");
		return -EPERM;
	}

	PDEBUG( "slow bit rate setting is phase_seg1 0x%08x	phase_seg2 0x%08x  sjw 0x%08x  prescaler 0x%02x    prop_seg is 0x%02x\n",bt->phase_seg1,bt->phase_seg2,bt->sjw,bt->brp,bt->prop_seg);
	PDEBUG( "fast bit rate setting is phase_seg1 0x%08x	phase_seg2 0x%08x  sjw 0x%08x  prescaler 0x%02x    prop_seg is 0x%02x\n",dbt->phase_seg1,dbt->phase_seg2,dbt->sjw,dbt->brp,dbt->prop_seg);
	
	bittiming_temp = ((bt->phase_seg1 + bt->prop_seg + 1 - 2) << SEG_1_SHIFT)|\
			 ((bt->phase_seg2 - 1) << SEG_2_SHIFT)| \
			 ((bt->sjw - 1 ) << SJW_SHIFT)| \
			 ((bt->brp - 1) << PRESC_SHIFT);

	/*The input bittime setting is incorrect, should be correct*/
	if( (((int)(bt->phase_seg1 + bt->prop_seg + 1) - 2) < 0) || \
		(((int)(bt->phase_seg2) - 1) < 0) || \
		(((int)(bt->sjw) - 1) < 0) || \
		(((int)(bt->brp) - 1) < 0) ){
		PDEBUG("slow bittime configuration is incorrect\n");
		return -EINVAL;
	}

	PDEBUG( "bittiming_temp value is %08x\n",bittiming_temp);
	priv->write_reg(priv,CANFD_S_SEG_1_OFFSET,bittiming_temp);
	
	if(priv->can_or_canfd == CANFD_MASK){ 
		
		data_bittiming_temp = ((dbt->phase_seg1 + dbt->prop_seg + 1 - 2) << SEG_1_SHIFT)|\
					((dbt->phase_seg2 - 1 ) << SEG_2_SHIFT)|\
					((dbt->sjw - 1 ) << SJW_SHIFT)|\
					((dbt->brp - 1) << PRESC_SHIFT);
		
		if( (((int)(dbt->phase_seg1 + dbt->prop_seg + 1) - 2) < 0) ||\
			(((int)(dbt->phase_seg2) - 1) < 0) || \
			(((int)(dbt->sjw) - 1) < 0) || \
			(((int)(dbt->brp) - 1) < 0) ){
			PDEBUG("fast bittime configuration is incorrect\n");
			return -EINVAL;
		}
			
		priv->write_reg(priv, CANFD_F_SEG_1_OFFSET, data_bittiming_temp);
	}
	
	/*reset off*/
	canfd_reigister_off_bit(priv, CANFD_CFG_STAT_OFFSET, CAN_FD_OFF_RST_MASK);

	/*print configured slow and fast bit rate*/
	PDEBUG( "Slow bit rate configuration: %08x\n",priv->read_reg(priv,CANFD_S_SEG_1_OFFSET));
	PDEBUG( "Fast bit rate configuration: %08x\n",priv->read_reg(priv,CANFD_F_SEG_1_OFFSET));
	return 0;
}

int canfd_get_freebuffer(struct IPMS_canfd_priv *priv){

	/*get next transmit buffer. If all buffers are full, return -1*/
	canfd_reigister_set_bit(priv,CANFD_TCTRL_OFFSET,CAN_FD_SET_TENEXT_MASK);
	if(can_ioread8(priv->reg_base+CANFD_TCTRL_OFFSET)&CAN_FD_SET_TENEXT_MASK){
		return -1;
	}
	return 0;
}

static void canfd_tx_interrupt(struct net_device *ndev,u8 isr){
	struct IPMS_canfd_priv *priv= netdev_priv(ndev);

	/*wait till transmission of the PTB or STB finished*/
	while(isr & (CAN_FD_SET_TPIF_MASK|CAN_FD_SET_TSIF_MASK)){
		if(isr & CAN_FD_SET_TPIF_MASK)
			canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,CAN_FD_SET_TPIF_MASK);

		if(isr & CAN_FD_SET_TSIF_MASK)
			canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,CAN_FD_SET_TSIF_MASK);
		
		//TODO: time out error
		isr = can_ioread8(priv->reg_base+CANFD_RTIF_OFFSET);
	}
	netif_wake_queue(ndev);
}

/*
 * Return: 1 on success and 0 on failure.
 */
static int can_rx(struct net_device *ndev){
	struct IPMS_canfd_priv *priv= netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;

	u32 ctl,can_id,data[2]={0,0};
	u8  dlc,control,rx_status;
	int i;

	rx_status=can_ioread8(priv->reg_base+CANFD_RCTRL_OFFSET);
	if(rx_status & CAN_FD_RSTAT_NOT_EMPTY_MASK){
		
		control=can_ioread8(priv->reg_base+CANFD_RBUF_CTL_OFFSET);
		can_id=priv->read_reg(priv,CANFD_RUBF_ID_OFFSET);
		dlc=can_ioread8(priv->reg_base+CANFD_RBUF_CTL_OFFSET)&CAN_FD_SET_DLC_MASK;

		skb = alloc_can_skb(ndev,(struct can_frame **)&cf);
		if(!skb){
			stats->rx_dropped++;
			return 0;

		}
		
		/*read  CAN data length*/
		cf->can_dlc=can_cc_dlc2len(dlc);
		PDEBUG("DLC in can_rx is %02x",dlc);    
		/*change the CANFD id into socketcan id format*/
		if(control&CAN_FD_SET_IDE_MASK){
			/*extended format*/
			cf->can_id=can_id;
			cf->can_id |=CAN_EFF_FLAG ;
			PDEBUG("ID Extension\n");   
			/*bit 29,error message not defined here*/
			//TODO:error message 
		}
		else{
			cf->can_id=can_id;
			cf->can_id &=(~CAN_EFF_FLAG) ;
			PDEBUG("without ID Extension\n");   
		}

		if(control&CAN_FD_SET_RTR_MASK)
			cf->can_id |=CAN_RTR_FLAG ;

		/* Data*/
		if(!(control&CAN_FD_SET_RTR_MASK)){
			*((u32*)(cf->data+0))= priv->read_reg(priv,CANFD_RBUF_DATA_OFFSET);
			*((u32*)(cf->data+4))= priv->read_reg(priv,CANFD_RBUF_DATA_OFFSET+4);
		}

		/*point to next packet and will save to buffer*/    
		/*reset RREL to release RB slot, next RBUF will be updated. Then reset receive interrupt flag(we can not reset interrupt flag here)*/
		canfd_reigister_set_bit(priv,CANFD_RCTRL_OFFSET,CAN_FD_SET_RREL_MASK);

		/*do some statistics*/
		stats->rx_bytes += can_fd_dlc2len(cf->can_dlc);
		stats->rx_packets++;
		i=netif_receive_skb(skb);
		if (i != NET_RX_SUCCESS) {
			// TODO: error handling
			PDEBUG( "test in receive interrupt. skb->dev->stats.rx_bytes is %d.\n",skb->dev->stats.rx_bytes);
			PDEBUG( "netif_receive_skb return value is %d\n",i);
		}

		return 1;
	}
		
	return 0;
}

/*
 * Return: 1 on success and 0 on failure.
 */
static int canfd_rx(struct net_device *ndev){
	struct IPMS_canfd_priv *priv= netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct canfd_frame *cf;
	struct sk_buff *skb;

	u32 ctl,can_id,data=0,data_can[2]={0,0};
	u8  dlc,control,rx_status;
	int i;

	/*since here we start canfd_rx interrrupt*/
	PDEBUG("start canfd_rx interrrupt\n");

	/*rx control register has 32 bits--->u32 ctl will be used later*/   
	rx_status=can_ioread8(priv->reg_base+CANFD_RCTRL_OFFSET);
	if(rx_status & CAN_FD_RSTAT_NOT_EMPTY_MASK){
		control=can_ioread8(priv->reg_base+CANFD_RBUF_CTL_OFFSET);
		can_id=priv->read_reg(priv,CANFD_RUBF_ID_OFFSET);
		dlc=can_ioread8(priv->reg_base+CANFD_RBUF_CTL_OFFSET)&CAN_FD_SET_DLC_MASK;

		if(control&CAN_FD_SET_EDL_MASK)
			/* allocate sk_buffer for canfd frame */
			skb = alloc_canfd_skb(ndev,&cf);
		else
			/* allocate sk_buffer for can frame */
			skb = alloc_can_skb(ndev,(struct can_frame **)&cf);

		if(!skb){
			stats->rx_dropped++;
			return 0;

		}   
		/*change the CANFD or CAN2.0 data into socketcan data format*/

		if(control&CAN_FD_SET_EDL_MASK)
			cf->len=can_fd_dlc2len(dlc);
		else
			cf->len=can_cc_dlc2len(dlc);

		/*change the CANFD id into socketcan id format*/
		if(control&CAN_FD_SET_EDL_MASK){
			/*extended format*/
			cf->can_id=can_id;
			if(control & CAN_FD_SET_IDE_MASK)
			cf->can_id |= CAN_EFF_FLAG;
			else
			cf->can_id &= (~CAN_EFF_FLAG);
			/*bit 29,error message not defined here*/

		}
		else{
			cf->can_id=can_id;
			if(control & CAN_FD_SET_IDE_MASK)
			cf->can_id |= CAN_EFF_FLAG;
			else
			cf->can_id &= (~CAN_EFF_FLAG);
			/*deal with RTR in can2.0*/
			if(control&CAN_FD_SET_RTR_MASK)
			cf->can_id |=CAN_RTR_FLAG ;

		}

		/* Data*/
		/*CANFD frames handed over to SKB*/
		if(control&CAN_FD_SET_EDL_MASK){
			for (i = 0; i < cf->len; i += 4) {
				PDEBUG( "CAN FD original DATA 0x%08x\n",priv->read_reg(priv,CANFD_RBUF_DATA_OFFSET+i));
				*((u32 *)(cf->data+i)) = priv->read_reg(priv,CANFD_RBUF_DATA_OFFSET+i);
			}
		}
		else{
			/*skb reads the received datas, if the RTR bit not set.*/
			if(!(control&CAN_FD_SET_RTR_MASK)){
				*((u32*)(cf->data+0))= priv->read_reg(priv,CANFD_RBUF_DATA_OFFSET);
				*((u32*)(cf->data+4))= priv->read_reg(priv,CANFD_RBUF_DATA_OFFSET+4);
			}
		}

		/*point to next packet and will save to buffer*/
		/*reset RREL to release RB slot, next RBUF will be updated. Then reset receive interrupt flag(we can not reset interrupt flag here)*/
		canfd_reigister_set_bit(priv,CANFD_RCTRL_OFFSET,CAN_FD_SET_RREL_MASK);
		//canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,CAN_FD_SET_RIF_MASK);  
		
		stats->rx_bytes += cf->len;
		stats->rx_packets++;
		i=netif_receive_skb(skb);
		
		if (i != NET_RX_SUCCESS) {
			// TODO: error handling
			PDEBUG( "test in receive interrupt. skb->dev->stats.rx_bytes is %d.\n",skb->dev->stats.rx_bytes);
			PDEBUG( "netif_receive_skb return value is %d\n",i);
		}

		return 1;
	}
	return 0;
}


static int canfd_rx_poll(struct napi_struct *napi, int quota){
	struct net_device *ndev = napi->dev;
	struct IPMS_canfd_priv *priv= netdev_priv(ndev);
	int work_done = 0;
	u8 rx_status=0,control=0;


	control=can_ioread8(priv->reg_base + CANFD_RBUF_CTL_OFFSET);
	rx_status = can_ioread8(priv->reg_base + CANFD_RCTRL_OFFSET);

	/*clear receive interrupt and deal with all the received frames*/
	PDEBUG("CANFD RX POLL");

	//canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,CAN_FD_SET_RIF_MASK);
	while((rx_status&CAN_FD_RSTAT_NOT_EMPTY_MASK)&&(work_done < quota)){

		( control & CAN_FD_SET_EDL_MASK) ? (work_done += canfd_rx(ndev)) : (work_done += can_rx(ndev));

		control=can_ioread8(priv->reg_base + CANFD_RBUF_CTL_OFFSET);
		rx_status = can_ioread8(priv->reg_base + CANFD_RCTRL_OFFSET);
	}

	//if (work_done < 10) {
	napi_complete(napi);
	canfd_reigister_set_bit(priv,CANFD_RTIE_OFFSET, CAN_FD_SET_RIE_MASK);
	//canfd_reigister_set_bit(priv,CANFD_RTIE_OFFSET,   CAN_FD_SET_RFIE_MASK);
	//canfd_reigister_set_bit(priv,CANFD_RTIE_OFFSET,   CAN_FD_SET_RAFIE_MASK);
	//}

	PDEBUG("CANFD RX POLL END");
	return work_done;   
}


static void canfd_rxfull_interrupt(struct net_device *ndev,u8 isr){
	struct IPMS_canfd_priv *priv= netdev_priv(ndev);

	if(isr & CAN_FD_SET_RAFIF_MASK)
		canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,CAN_FD_SET_RAFIF_MASK);

	if (isr & (CAN_FD_SET_RAFIF_MASK|CAN_FD_SET_RFIF_MASK) )
		canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,(CAN_FD_SET_RAFIF_MASK|CAN_FD_SET_RFIF_MASK));
}


/*Mode could be chosen, by reading corresponding registers.*/
/*This part could also be removed, when only one mode is used*/
static int set_canfd_xmit_mode(struct net_device *ndev){

	struct IPMS_canfd_priv *priv = netdev_priv(ndev);
	
	switch(priv->tx_mode){
		case XMIT_FULL:
			canfd_reigister_set_bit(priv,CANFD_TCTRL_OFFSET,CAN_FD_SET_FULLCAN_MASK);
			PDEBUG( "Full can mode\n");
			break;

		case XMIT_SEP_FIFO:
			canfd_reigister_off_bit(priv,CANFD_TCTRL_OFFSET,CAN_FD_OFF_FULLCAN_MASK);
			canfd_reigister_set_bit(priv,CANFD_TCTRL_OFFSET,CAN_FD_SET_FIFO_MASK);
			canfd_reigister_off_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_SET_TBSEL_MASK);
			PDEBUG( "FIFO mode\n");     

			break;

		case XMIT_SEP_PRIO:
			canfd_reigister_off_bit(priv,CANFD_TCTRL_OFFSET,CAN_FD_OFF_FULLCAN_MASK);
			canfd_reigister_off_bit(priv,CANFD_TCTRL_OFFSET,CAN_FD_OFF_FIFO_MASK);
			canfd_reigister_off_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_SET_TBSEL_MASK);
			PDEBUG( "Priority mode\n");     

			break;      
		case XMIT_PTB_MODE:
			canfd_reigister_off_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_OFF_TBSEL_MASK);/*PTB is used here*/
			PDEBUG( "PTB mode\n");      

			break;
			
		default:    
		break;
		}   
	return 0;
}

/*
 * netdev_tx_t (*ndo_start_xmit)(struct sk_buff *skb,
 *                               struct net_device *dev);
 *  Called when a packet needs to be transmitted.
 *  Must return NETDEV_TX_OK , NETDEV_TX_BUSY.
 *        (can also return NETDEV_TX_LOCKED iff NETIF_F_LLTX)
 *  Required can not be NULL.
*/
static int canfd_driver_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	u32 ttsen,id,temp,ctl,addr_off;
	int i;
	struct IPMS_canfd_priv *priv = netdev_priv(ndev);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	struct net_device_stats *stats = &ndev->stats;

	PDEBUG( "CANFD Driver starts to send canfd/can 2.0 frame since here\n");
	priv->tx_mode = XMIT_PTB_MODE;//test

	switch(priv->tx_mode){
		case XMIT_FULL:

			return -1;
			break;

		case XMIT_PTB_MODE:
			PDEBUG( "PTB XMIT Mode\n");
			
			/*set transmit register*/
			set_canfd_xmit_mode(ndev);

			/*select TBSEL, frame writen in PTB */
			//canfd_reigister_set_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_SET_TBSEL_MASK);

			canfd_reigister_off_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_OFF_STBY_MASK);/*standby mode off*/
			PDEBUG( "CFG_STAT is %02x after STBY off\n",can_ioread8(priv->reg_base+CANFD_CFG_STAT_OFFSET));
			PDEBUG( "TCMD is %02x after STBY off\n",can_ioread8(priv->reg_base+CANFD_TCMD_OFFSET));

			if(cf->can_id&CAN_EFF_FLAG){
				/*extended îd length, CAN_EFF_FLAG is set in the msb*/
				id =(cf->can_id & CAN_EFF_MASK);
				ttsen=0<<TTSEN_8_32_SHIFT;
				//ttsen=CAN_FD_SET_TTSEN_MASK<<TTSEN_8_32_SHIFT;
				id|=ttsen;  
			}else{
				/*standard id*/
				id =(cf->can_id & CAN_SFF_MASK);
				ttsen=0<<TTSEN_8_32_SHIFT;
				//ttsen=CAN_FD_SET_TTSEN_MASK<<TTSEN_8_32_SHIFT;
				id|=ttsen;    
			}

			PDEBUG("cf->can_id=%x10",cf->can_id);

			ctl=can_fd_len2dlc(cf->len);
			
			/*transmit can fd frame*/
			if(priv->can_or_canfd==CANFD_MASK){
				if (can_is_canfd_skb(skb)) {
					if(cf->can_id&CAN_EFF_FLAG)
						ctl |= CAN_FD_SET_IDE_MASK; /*IDE=1 if IDenfitier Extension*/
					else
						ctl &=CAN_FD_OFF_IDE_MASK;/*IDE=0*/

					if (cf->flags & CANFD_BRS)	/*CANFD_BRS=0x01*/
						ctl |= CAN_FD_SET_BRS_MASK;
					
					ctl |= CAN_FD_SET_EDL_MASK;

					addr_off=CANFD_TBUF_DATA_OFFSET;
					
					for(i = 0; i < cf->len; i += 4){
						PDEBUG( "CAN FD original DATA 0x%08x\n",priv->read_reg(priv,addr_off));
						
						priv->write_reg(priv,addr_off,*((u32 *)(cf->data+i)));
						PDEBUG("data in transmit register is:0x%08x\n",ioread32(priv->reg_base+addr_off));
						PDEBUG( "CAN FD updated DATA 0x%08x\n",*((u32 *) (cf->data+i)));
						addr_off+=4;
					}

					PDEBUG( "TCMD is 0x%02x after writing data into resiger\n",can_ioread8(priv->reg_base+CANFD_TCMD_OFFSET));
					PDEBUG( "Interrupt flag register is 0x%02x after writing data into resiger\n",can_ioread8(priv->reg_base+CANFD_RTIF_OFFSET));
				}   
				
				//can_put_echo_skb(skb, ndev, 0);/*have no idea with this statement*/
				/*temp=canfd_get_freebuffer(priv);
				if(temp==-1){
				netif_stop_queue(ndev); 
				}
				
				*/
				/* Transmit can 2.0 frame under canfd mode*/
				else{
					ctl &= CAN_FD_OFF_EDL_MASK;//set Extended Data  Length 0, force BRS to be set 0 ;
					ctl &= CAN_FD_OFF_BRS_MASK;
				
					if(cf->can_id&CAN_EFF_FLAG)
						ctl |= CAN_FD_SET_IDE_MASK; /*IDE=1 if IDenfitier Extension*/
					else
						ctl &= CAN_FD_OFF_IDE_MASK;/*IDE=0*/

					if(cf->can_id & CAN_RTR_FLAG){
						ctl |= CAN_FD_SET_RTR_MASK;
						priv->write_reg(priv,CANFD_TBUF_ID_OFFSET, id);
						priv->write_reg(priv,CANFD_TBUF_CTL_OFFSET,ctl);
					}
					else{
						ctl &= CAN_FD_OFF_RTR_MASK;
						addr_off = CANFD_TBUF_DATA_OFFSET;
						PDEBUG( "CAN original DATA 0x%08x\n",priv->read_reg(priv,addr_off));
						priv->write_reg(priv,addr_off,*((u32 *)(cf->data+0)));
						PDEBUG( "CAN updated DATA 0x%08x\n",priv->read_reg(priv,addr_off));
						PDEBUG( "CAN original DATA 0x%08x\n",priv->read_reg(priv,addr_off+4));
						priv->write_reg(priv,addr_off+4,*((u32 *)(cf->data+4)));
						PDEBUG( "CAN updated DATA 0x%08x\n",priv->read_reg(priv,addr_off+4));

						PDEBUG( "TCMD is 0x%02x after writing data into resiger\n",can_ioread8(priv->reg_base+CANFD_TCMD_OFFSET));
						PDEBUG( "Interrupt flag register is 0x%02x after writing data into resiger\n",can_ioread8(priv->reg_base+CANFD_RTIF_OFFSET));

					}

				}
				priv->write_reg(priv,CANFD_TBUF_ID_OFFSET, id);
				priv->write_reg(priv,CANFD_TBUF_CTL_OFFSET,ctl);

				PDEBUG( "ID 0x%08x\n",priv->read_reg(priv,CANFD_TBUF_ID_OFFSET));
				PDEBUG( "Control 0x%08x\n",priv->read_reg(priv,CANFD_TBUF_CTL_OFFSET));

				addr_off = CANFD_TBUF_DATA_OFFSET;
				PDEBUG( "TCMD is 0x%02x before set TPE=1\n",can_ioread8(priv->reg_base+CANFD_TCMD_OFFSET));

			}
			
		/*deal with can 2.0 frame, when the driver can only be compatible with can 2.0 tranceiver   */  
		else{
			/*transmit can2.0 frame*/
			PDEBUG("Sending can2.0 frame\n");

			ctl &= CAN_FD_OFF_EDL_MASK;//set Extended Data  Length 0, force BRS to be set 0 ;
			ctl &= CAN_FD_OFF_BRS_MASK;
			
			if(cf->can_id&CAN_EFF_FLAG)
				ctl |= CAN_FD_SET_IDE_MASK; /*IDE=1 if IDenfitier Extension*/
			else
				ctl &= CAN_FD_OFF_IDE_MASK;/*IDE=0*/

			if(cf->can_id & CAN_RTR_FLAG){
				ctl |= CAN_FD_SET_RTR_MASK;
				priv->write_reg(priv,CANFD_TBUF_ID_OFFSET, id);
				priv->write_reg(priv,CANFD_TBUF_CTL_OFFSET,ctl);
			}
			else{
				ctl &= CAN_FD_OFF_RTR_MASK;

				priv->write_reg(priv,CANFD_TBUF_ID_OFFSET, id);
				priv->write_reg(priv,CANFD_TBUF_CTL_OFFSET,ctl);

				PDEBUG( "ID 0x%08x\n",priv->read_reg(priv,CANFD_TBUF_ID_OFFSET));
				PDEBUG( "Control 0x%08x\n",priv->read_reg(priv,CANFD_TBUF_CTL_OFFSET));

				addr_off=CANFD_TBUF_DATA_OFFSET;
				PDEBUG( "CAN original DATA 0x%08x\n",priv->read_reg(priv,addr_off));
				priv->write_reg(priv,addr_off,*((u32 *)(cf->data+0)));
				PDEBUG( "CAN updated DATA 0x%08x\n",priv->read_reg(priv,addr_off));
				PDEBUG( "CAN original DATA 0x%08x\n",priv->read_reg(priv,addr_off+4));
				priv->write_reg(priv,addr_off+4,*((u32 *)(cf->data+4)));
				PDEBUG( "CAN updated DATA 0x%08x\n",priv->read_reg(priv,addr_off+4));

				PDEBUG( "TCMD is 0x%02x after writing data into resiger\n",can_ioread8(priv->reg_base+CANFD_TCMD_OFFSET));
				PDEBUG( "Interrupt flag register is 0x%02x after writing data into resiger\n",can_ioread8(priv->reg_base+CANFD_RTIF_OFFSET));

			}

		}   
		
		/*set TPE to transmit data. update statistic*/
		canfd_reigister_set_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_SET_TPE_MASK);
		stats->tx_bytes += cf->len;
		//canfd_reigister_set_bit(priv,CANFD_RCTRL_OFFSET,CAN_FD_SET_SACK_MASK);/*set SACK---->loop back external*/

		/*stop transmit queue*/
		netif_stop_queue(ndev); 

		//canfd_reigister_set_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_SET_TPA_MASK);
		//canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,0x08);
		//PDEBUG( "TPA is %d\n",can_ioread8(priv->reg_base+CANFD_TCMD_OFFSET)&CAN_FD_SET_TPA_MASK>>3);
		//netif_stop_queue(ndev);   
		
		/*send interrupt flag debug*/
		PDEBUG( "Interrupt flag register is 0x%02x. TX ends\n",can_ioread8(priv->reg_base+CANFD_RTIF_OFFSET));
		PDEBUG( "Interrupt enable register is 0x%02x. TX ends\n",can_ioread8(priv->reg_base+CANFD_RTIE_OFFSET));

		PDEBUG( "Transmission status is %d. TX ends\n",can_ioread8(priv->reg_base+CANFD_CFG_STAT_OFFSET)&CAN_FD_TASCTIVE_MASK>>1);

		PDEBUG( "Receive buffer:Data first 8 bytes 0x%08x. TX ends\n",ioread32(priv->reg_base+CANFD_RBUF_DATA_OFFSET));
		PDEBUG( "TCMD is 0x%02x. TX ends\n",can_ioread8(priv->reg_base+CANFD_TCMD_OFFSET));
		PDEBUG( "Receive buffer:id 0x%08x. TX ends\n",ioread32(priv->reg_base+CANFD_RUBF_ID_OFFSET));
		PDEBUG( "Receive buffer:Control 0x%08x. TX ends\n",ioread32(priv->reg_base+CANFD_RBUF_CTL_OFFSET));
		PDEBUG( "Receive buffer:Data first 8 bytes 0x%08x. TX ends\n",ioread32(priv->reg_base+CANFD_RBUF_DATA_OFFSET));
		PDEBUG( "Interrupt flag register is 0x%02x. TX ends\n",can_ioread8(priv->reg_base+CANFD_RTIF_OFFSET));
		break;
		
		default:
		break;  
		
	}
	
	return NETDEV_TX_OK;
}

static int set_reset_mode(struct net_device *ndev){
	struct IPMS_canfd_priv *priv = netdev_priv(ndev);
	u8 ret;

	ret=can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);
	ret|=CAN_FD_SET_RST_MASK;
	can_iowrite8(ret, priv->reg_base + CANFD_CFG_STAT_OFFSET);
	return 0;
}

static void canfd_driver_stop(struct net_device *ndev)
{
	struct IPMS_canfd_priv *priv = netdev_priv(ndev);
	int ret;
	
	/* Disable interrupts and leave the can in configuration mode */
	ret = set_reset_mode(ndev);
	if (ret < 0)
		PDEBUG( "set reset mod failed\n");

	priv->can.state = CAN_STATE_STOPPED;
}

static int canfd_driver_close(struct net_device *ndev)
{
	struct IPMS_canfd_priv *priv = netdev_priv(ndev);

	PDEBUG( "can stops here\n");
	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	canfd_driver_stop(ndev);
	free_irq(ndev->irq, ndev);
	close_candev(ndev);
	return 0;
}

static enum can_state get_of_chip_status(struct net_device *ndev){
	struct IPMS_canfd_priv *priv = netdev_priv(ndev);
	u8  can_stat,eir;

	can_stat=can_ioread8(priv->reg_base+CANFD_CFG_STAT_OFFSET);
	eir = can_ioread8(priv->reg_base + CANFD_ERRINT_OFFSET);

	if(can_stat & CAN_FD_SET_BUSOFF_MASK) 
		return CAN_STATE_BUS_OFF;
	
	if((eir & CAN_FD_SET_EPASS_MASK) && ~(can_stat & CAN_FD_SET_BUSOFF_MASK))
		return CAN_STATE_ERROR_PASSIVE;
	
	if(eir & CAN_FD_SET_EWARN_MASK && ~(eir & CAN_FD_SET_EPASS_MASK)) 
		return CAN_STATE_ERROR_WARNING;
	
	if(~(eir & CAN_FD_SET_EPASS_MASK)) 
		return CAN_STATE_ERROR_ACTIVE;
}

/*Error Interrupt Routine*/
static void canfd_error_interrupt(struct net_device *ndev, u8 isr, u8 eir){
	struct IPMS_canfd_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u8  koer,recnt=0,tecnt=0,can_stat=0;

	DEV_PRINT("in Error interrupt:")
	skb = alloc_can_err_skb(ndev, &cf);

	koer = can_ioread8(priv->reg_base +CANFD_EALCAP_OFFSET)&CAN_FD_SET_KOER_MASK;
	recnt= can_ioread8(priv->reg_base +CANFD_RECNT_OFFSET);
	tecnt= can_ioread8(priv->reg_base +CANFD_TECNT_OFFSET); 

	/*read CAN Status*/
	can_stat=can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);

	PDEBUG("canfd_error_interrupt starts since here\n");
	PDEBUG("At the beginning of error interrupt, isr is 0x%02x and eir is 0x%02x\n",isr,eir);
	PDEBUG("canfd error counter: RECNT is 0x%02x, TECNT is 0x%02x\n",can_ioread8(priv->reg_base+CANFD_RECNT_OFFSET),can_ioread8(priv->reg_base+CANFD_TECNT_OFFSET));
	PDEBUG("KOER is :0x%02x at the beginning\n",koer);
	PDEBUG("canfd is %s the beginning\n",(can_stat & CAN_FD_SET_BUSOFF_MASK)?"busoff":"not busoff");
	/*define the status of can, when error exists*/

	/*BUS-OFF->active error mode*/
	if( (isr & CAN_FD_SET_EIF_MASK) && (priv->can.state==CAN_STATE_BUS_OFF)){
		//TODO: restart functions should be finished
		//can_restart_now(ndev);  //not sure,because dev.c describes->only can be used if automatic restart is disabled and the device is in the bus-off state. How to know if it could be automatically called?
		priv->can.state=get_of_chip_status(ndev);
	}

	/*bus off*/
	/*if status is BUSOFF*/
	if(can_stat & CAN_FD_SET_BUSOFF_MASK){
		
		PDEBUG("CFG_STAT(BUSOFF bit) in EIF branch: 0x%02x\n",can_ioread8(priv->reg_base+CANFD_CFG_STAT_OFFSET));
		PDEBUG("BUSOFF exists\n");
		/*abortion of not transmitted frame*/
		//canfd_reigister_set_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_SET_TPA_MASK);
		
		/*statistics*/
		priv->can.state=get_of_chip_status(ndev);
		priv->can.can_stats.bus_off++;
		
		/*reset canfd ip core*/
		//test with reset busoff status
		canfd_reigister_set_bit(priv,CANFD_CFG_STAT_OFFSET,CAN_FD_SET_BUSOFF_MASK);
		//canfd_reigister_set_bit(priv,CANFD_CFG_STAT_OFFSET,CAN_FD_SET_RST_MASK);

		/*tell netif layer,no more packets*/
		//TODO:configure the can to bus-off status?
		can_bus_off(ndev);
		
		if (skb)
			cf->can_id |= CAN_ERR_BUSOFF;
		
	}
	else if((eir & CAN_FD_SET_EPASS_MASK) && ~(can_stat & CAN_FD_SET_BUSOFF_MASK)){
	
		/*Error Passive mode*/
		/*EPASS is either 0 or 1. Hypothetically, when transceiver is in BUSOFF mode, bit EPASS stays unchanged, i.e. "1" */
		PDEBUG("Error Interrupt in Error Passive mode\n");
		priv->can.state = get_of_chip_status(ndev);
		priv->can.can_stats.error_passive++;
		if(skb){
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= (recnt>127)?CAN_ERR_CRTL_RX_PASSIVE:0;
			cf->data[1] |= (tecnt>127)?CAN_ERR_CRTL_TX_PASSIVE:0;
			cf->data[6]=tecnt;
			cf->data[7]=recnt;
		}
		
	}
	else if(eir & CAN_FD_SET_EWARN_MASK && ~(eir & CAN_FD_SET_EPASS_MASK)){
	
		/*Counter over Error limitation*/

		PDEBUG("Error Warning\n");
		priv->can.state =get_of_chip_status(ndev);
		priv->can.can_stats.error_warning++;
		if(skb){
			/*Bosch specification indicates that Error count value greater than 96 is heavily disturbed.*/
			/*EWL=11*/
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= (recnt>95)?CAN_ERR_CRTL_RX_WARNING:0;
			cf->data[1] |= (tecnt>95)?CAN_ERR_CRTL_TX_WARNING:0;
			cf->data[6]=tecnt;
			cf->data[7]=recnt;
		}
		
	}


	PDEBUG("CFG_STAT(BUSOFF bit) before protocol errors: 0x%02x\n",can_ioread8(priv->reg_base+CANFD_CFG_STAT_OFFSET));
	
	/* Check for in protocol defined error interrupt */
	if(eir & CAN_FD_SET_BEIF_MASK){
		
		PDEBUG("protocol defined error exists\n");
		PDEBUG("KOER is :0x%02x in protocol error \n",can_ioread8(priv->reg_base +CANFD_EALCAP_OFFSET)&CAN_FD_SET_KOER_MASK);       
	
		if(skb)
			cf->can_id |= CAN_ERR_BUSERROR  | CAN_ERR_PROT;
		
		/*BIT error interrupt*/ 
		if(koer == CAN_FD_SET_BIT_ERROR_MASK){
			PDEBUG("BIT error exists\n");
			stats->tx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_BIT;
			}
		}

		/*FORM error interrupt*/    
		if(koer == CAN_FD_SET_FORM_ERROR_MASK){
			
			PDEBUG("FORM error exists\n");
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_FORM;
			}
		}

		/*STUFF error interrupt*/
		if(koer == CAN_FD_SET_STUFF_ERROR_MASK){
			
			PDEBUG("STUFF error exists\n");
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[3] = CAN_ERR_PROT_STUFF;
			}
		}

		/*ACK error interrupt*/ 
		if(koer == CAN_FD_SET_ACK_ERROR_MASK){
			
			PDEBUG("ACK error exists\n");
			/*test with abortion*/
			//canfd_reigister_set_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_SET_TPA_MASK);

			stats->tx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_LOC_ACK;
			}
		}

		/*CRC error interrupt*/ 
		if(koer == CAN_FD_SET_CRC_ERROR_MASK){
			
			PDEBUG("CRC error exists\n");
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_LOC_CRC_SEQ ;
			}
		}
		priv->can.can_stats.bus_error++;
	}
	
	//canfd_reigister_set_bit(priv,CANFD_TCMD_OFFSET,CAN_FD_SET_TPA_MASK);
	//priv->can.state = TODO_GET_STATUS_OF_CHIP(struct net_device *ndev);

	if (skb) {
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}
	
	/*interrupt enable*/
	//canfd_reigister_set_bit(priv,CANFD_ERRINT_OFFSET,CAN_FD_SET_EPIE_MASK|CAN_FD_SET_BEIE_MASK);
	//canfd_reigister_set_bit(priv,CANFD_RTIE_OFFSET,CAN_FD_SET_EIE_MASK);
	
	PDEBUG("canfd error counter: RECNT is 0x%02x, TECNT is 0x%02x\n",can_ioread8(priv->reg_base+CANFD_RECNT_OFFSET),can_ioread8(priv->reg_base+CANFD_TECNT_OFFSET));
	PDEBUG("canfd_error_interrupt ends\n");
}

static irqreturn_t canfd_interrupt(int irq, void *dev_id){
	struct net_device *ndev = (struct net_device *)dev_id;
	struct IPMS_canfd_priv *priv = netdev_priv(ndev);

	u8 isr,eir;
	u8 isr_handled, eir_handled;
	
	/*read the value of interrupt status register and error interrupt register*/
	isr = can_ioread8(priv->reg_base + CANFD_RTIF_OFFSET);
	eir = can_ioread8(priv->reg_base + CANFD_ERRINT_OFFSET);

	isr_handled = 0;
	eir_handled = 0;

	/* Check for Tx interrupt and Processing it */
	if (isr & (CAN_FD_SET_TPIF_MASK|CAN_FD_SET_TSIF_MASK)){
		canfd_tx_interrupt(ndev, isr);  
		isr_handled |= (CAN_FD_SET_TPIF_MASK|CAN_FD_SET_TSIF_MASK);
		PDEBUG("tx interrupt\n");
	}

	if (isr & (CAN_FD_SET_RAFIF_MASK|CAN_FD_SET_RFIF_MASK) ) {
		canfd_rxfull_interrupt(ndev,isr);
		isr_handled |= (CAN_FD_SET_RAFIF_MASK|CAN_FD_SET_RFIF_MASK);
		PDEBUG("Rx rxfull interrupt\n");
	}

	/*  Check Rx interrupt and Processing the receive interrupt routine*/
	if (isr & CAN_FD_SET_RIF_MASK){
		canfd_reigister_off_bit(priv,CANFD_RTIE_OFFSET, CAN_FD_OFF_RIE_MASK);
		canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,CAN_FD_SET_RIF_MASK);
		napi_schedule(&priv->napi);
		isr_handled |= CAN_FD_SET_RIF_MASK;
		PDEBUG("Rx interrupt\n");
	}
	
	//test
	PDEBUG("RTIE before error routine is 0x%02x\n",can_ioread8(priv->reg_base+CANFD_RTIE_OFFSET));
	// if (~( (isr&CAN_FD_SET_EIF_MASK) || (eir & CAN_FD_SET_EPIF_MASK ) || (eir & CAN_FD_SET_BEIF_MASK))) {
	 //     return IRQ_NONE;
	 // }

	
	if ( (isr & CAN_FD_SET_EIF_MASK) | (eir & (CAN_FD_SET_EPIF_MASK | CAN_FD_SET_BEIF_MASK))){
		//if (isr & CAN_FD_SET_EIF_MASK){
		PDEBUG("Error Error Error\n");

		/*reset EPIF and BEIF. Reset EIF. What if during error interrupt, new interrupt appears?*/
		canfd_reigister_set_bit(priv,CANFD_ERRINT_OFFSET,eir & (CAN_FD_SET_EPIF_MASK|CAN_FD_SET_BEIF_MASK));//0x11
		canfd_reigister_set_bit(priv,CANFD_RTIF_OFFSET,isr & CAN_FD_SET_EIF_MASK);//0x2

		canfd_error_interrupt(ndev,isr,eir);
		
		isr_handled |= CAN_FD_SET_EIF_MASK;
		eir_handled |= (CAN_FD_SET_EPIF_MASK | CAN_FD_SET_BEIF_MASK);
	}
	
	if ((isr_handled == 0) && (eir_handled == 0)) {
		DEV_PRINT("unhandled interrupt!");
		return IRQ_NONE;
	}
	
	DEV_PRINT("ISR: %02X/%02X\n", isr, isr_handled);
	DEV_PRINT("EIR: %02X/%02X\n", eir, eir_handled);
	DEV_PRINT("canfd error counter: RECNT is 0x%02x, TECNT is 0x%02x\n",can_ioread8(priv->reg_base+CANFD_RECNT_OFFSET),can_ioread8(priv->reg_base+CANFD_TECNT_OFFSET));
	return IRQ_HANDLED;
}

static int canfd_chip_start(struct net_device *ndev){
	struct IPMS_canfd_priv *priv = netdev_priv(ndev);
	int temp;
	u8 ret;

	PDEBUG( "can starts to work here\n");

	temp=set_reset_mode(ndev);
	if (temp < 0){
		PDEBUG( "set reset mod failed\n");
		return temp;
	}
	
	temp=canfd_device_driver_bittime_configuration(ndev);/*set S_seg_1 S_Seg_2 S_SJW S_PRESC */
	if (temp < 0){
		PDEBUG( "bittime setting failed\n");
		return temp;
	}
	
	/*set Almost Full Warning Limit*/
	canfd_reigister_set_bit(priv,CANFD_LIMIT_OFFSET,CAN_FD_SET_AFWL_MASK);

	/*Programmable Error Warning Limit = (EWL+1)*8. Set EWL=11->Error Warning=96 */
	canfd_reigister_set_bit(priv,CANFD_LIMIT_OFFSET,CAN_FD_SET_EWL_MASK);
	
	/*interrupts enable*/
	can_iowrite8(CAN_FD_INTR_ALL_MASK,priv->reg_base + CANFD_RTIE_OFFSET);
	
	/*Error Interrupts enable(Error Passive and Bus Error)*/
	canfd_reigister_set_bit(priv,CANFD_ERRINT_OFFSET,CAN_FD_SET_EPIE_MASK);
	//canfd_reigister_set_bit(priv,CANFD_ERRINT_OFFSET,CAN_FD_SET_BEIE_MASK);
	
	/*check the mode,later on*/

	/*set mode*/
	ret=can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);
	/* Check whether it is loopback mode or normal mode  */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK){
		//ret|=CAN_FD_LBMEMOD_MASK; /*set loopback externel mode*/
		ret |= CAN_FD_LBMIMOD_MASK; /*set loopback internel mode*/
	}
	else{
		ret&=~CAN_FD_LBMEMOD_MASK;  /*unset loopback externel mode*/
		ret&=~CAN_FD_LBMIMOD_MASK;  /*unset loopback internel mode*/
	}

	can_iowrite8(ret, priv->reg_base + CANFD_CFG_STAT_OFFSET);  

	/*the start state*/
	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	PDEBUG( "canfd_chip_start end\n");

	return 0;

}


static int  canfd_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int ret;

	switch (mode) {
	case CAN_MODE_START:
		ret = canfd_chip_start(ndev);
		if (ret < 0) {
			PDEBUG( "canfd_chip_start failed!\n");
			return ret;
		}
		netif_wake_queue(ndev);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}


static int canfd_driver_open(struct net_device *ndev){

	struct IPMS_canfd_priv *priv = netdev_priv(ndev);
	int ret;
	/*test bittiming*/
	struct can_bittiming *bt = &priv->can.bittiming;
	struct can_bittiming *dbt = &priv->can.data_bittiming;
	
	/* Set chip into reset mode */
	ret = set_reset_mode(ndev);
	if (ret < 0) {
		netdev_err(ndev, "mode resetting failed!\n");
		goto err_irq;
	}

	PDEBUG( "set_reset_mod finished\n"); 
	ret = open_candev(ndev);
	if (ret)
		goto err_irq;
	PDEBUG( "open_candev finished\n");

	/*request irq*/
	ret = request_irq(ndev->irq, canfd_interrupt,IRQF_SHARED/* priv->irq_flags*/,ndev->name, ndev);
	if (ret < 0) {
		netdev_err(ndev, "irq allocation for CAN failed\n");
		goto err;
	}
	PDEBUG( "request_irq finished\n");
	
	ret = canfd_chip_start(ndev);
	if (ret < 0) {
		netdev_err(ndev, "canfd_chip_start failed!\n");
		goto err_candev;
	}
	
	PDEBUG( "canfd_chip_start finished\n");
	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	PDEBUG( "canfd_driver_open success\n");

	return 0;

//TODO: handle with errors
err_candev:
	PDEBUG("Error with starting canfd chip\n");
err_irq:
	PDEBUG("Error during opening can device\n");
	free_irq(ndev->irq, ndev);
err:
	PDEBUG("Error during requesting irq\n");

	return ret;
}       

static const struct net_device_ops canfd_netdev_ops = {
	.ndo_open = canfd_driver_open,
	.ndo_stop = canfd_driver_close,
	.ndo_start_xmit = canfd_driver_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int canfd_driver_probe(struct platform_device *pdev){
	struct net_device *ndev;
	struct IPMS_canfd_priv *priv;
	void __iomem *addr;
	int ret, tx_max;
	struct resource *res; 
	const struct of_device_id *match;

	/* Get the virtual base address for the device */
	addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto err;
	}
	
	/* Create a CAN device instance */
	tx_max=TX_MAX;
	ndev = alloc_candev(sizeof(struct IPMS_canfd_priv), tx_max);
	if (!ndev)
		return -ENOMEM;
	
	/*get the private part in net_device*/
	priv = netdev_priv(ndev);

	/*find compatible device in match_table*/
	match = of_match_node(canfd_of_match, pdev->dev.of_node);
	if(match && match->data){
		const struct ipms_platform_data *data = match->data;
		priv->can_or_canfd=data->can_or_canfd;
		PDEBUG("priv->can_or_canfd is %d, driver type: %s \n",priv->can_or_canfd ,priv->can_or_canfd?"CANFD":"CAN");
	}

	/*basic device strucure embedded in higher-level representation of device*/
	priv->dev = &pdev->dev;

	/*get the bittiming table, which shows bit-time-setting limitation  in user space*/ 
	priv->can.bittiming_const = &canfd_bittiming_const;
	priv->can.data_bittiming_const = &canfd_data_bittiming_const;
	priv->can.do_set_mode = canfd_do_set_mode;
	
	/*in user space the execution mode can be choosen */
	if(priv->can_or_canfd==CANFD_MASK){
		priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_FD;
	}
	else{
		//TODO: still have problem here 
		priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK;
	}


	/*basic device informations*/   
	priv->reg_base = addr;
	priv->tx_max = tx_max;
	priv->tx_head = 0;
	priv->tx_tail = 0;

	priv->write_reg = canfd_write_reg_le;
	priv->read_reg = canfd_read_reg_le;

	/*set bittime*/ 
	priv->can_clk = devm_clk_get(&pdev->dev, "ipms_can_clk");
	if (IS_ERR(priv->can_clk)) {
		dev_err(&pdev->dev, "Device clock not found.\n");
		ret = PTR_ERR(priv->can_clk);
		goto err;
	}

	priv->can.clock.freq = clk_get_rate(priv->can_clk);
	/*Get IRQ for the device*/
	ndev->irq = platform_get_irq(pdev,0);
	ndev->flags |= IFF_ECHO;    /*local echo is supported*/

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &canfd_netdev_ops;

	netif_napi_add(ndev,&priv->napi,canfd_rx_poll,16);//test budget=16

	ret =register_candev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "fail to register failed (err=%d)\n", ret);
		goto err;
	}
	
	PDEBUG("reg_base=0x%08x irq=%d clock=%d, tx fifo depth:%d\n", priv->reg_base, ndev->irq, priv->can.clock.freq,priv->tx_max);
	PDEBUG("reg_base=0x%08x irq=%d clock=%d\n",priv->reg_base, ndev->irq, priv->can.clock.freq);
	
	/*test to print the name of each transceiver*/
	PDEBUG("name of the transceiver: %s\n",ndev->name);
	PDEBUG("name of the transceiver in platform_device: %s\n",pdev->name);
	PDEBUG("end of probe\n");
	return 0;  
err:
	PDEBUG("error in probe function");
	return 1;
}

static int canfd_driver_remove(struct platform_device *pdev){
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct IPMS_canfd_priv *priv=netdev_priv(ndev);

	unregister_candev(ndev);
	netif_napi_del(&priv->napi);
	free_candev(ndev);
	return 0;
}

static struct platform_driver can_driver = {
	.probe          = canfd_driver_probe,
	.remove         = canfd_driver_remove,
	.driver = {
		.name  = DRIVER_NAME,
		.of_match_table = canfd_of_match,
	},
};

int canfd_driver_init(void){
	platform_driver_register(&can_driver);
	return 0;
}

void canfd_driver_exit(void){
	platform_driver_unregister(&can_driver);
	return;
}

module_init(canfd_driver_init);
module_exit(canfd_driver_exit);


MODULE_AUTHOR("jenny.zhang <jenny.zhang@starfivetech.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("IPMS can controller driver");
