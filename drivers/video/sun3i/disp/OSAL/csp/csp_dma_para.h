/*
*********************************************************************************************************
*											        eBase
*						                the Abstract of Hardware
*									           the OAL of DMA
*
*						        (c) Copyright 2006-2010, AW China
*											All	Rights Reserved
*
* File    	: 	dma.h
* Date		:	2010-06-24
* By      	: 	holigun
* Version 	: 	V1.00
*********************************************************************************************************
*/
#ifndef	__CSP_DMAC_PARA_H__
#define	__CSP_DMAC_PARA_H__

#define  CSP_DMA_HDLER_TYPE_CNT                     2
#define  CSP_DMAC_DMATYPE_NORMAL         			0
#define  CSP_DMAC_DMATYPE_DEDICATED      			1


#define  CSP_DMA_TRANSFER_HALF_INT       0
#define  CSP_DMA_TRANSFER_END_INT        1

#define  CSP_DMA_TRANSFER_UNLOOP_MODE   0
#define  CSP_DMA_TRANSFER_LOOP_MODE     1


//================================
//======    DMA 配置     =========
//================================

/* DMA 基础配置  */
#define CSP_DMAC_CFG_CONTINUOUS_ENABLE              (0x01)	//(0x01<<29)
#define CSP_DMAC_CFG_CONTINUOUS_DISABLE             (0x00)	//(0x01<<29)

//* DMA 等待时钟 */
#define	CSP_DMAC_CFG_WAIT_1_DMA_CLOCK				(0x00)	//(0x00<<26)
#define	CSP_DMAC_CFG_WAIT_2_DMA_CLOCK				(0x01)	//(0x01<<26)
#define	CSP_DMAC_CFG_WAIT_3_DMA_CLOCK				(0x02)	//(0x02<<26)
#define	CSP_DMAC_CFG_WAIT_4_DMA_CLOCK				(0x03)	//(0x03<<26)
#define	CSP_DMAC_CFG_WAIT_5_DMA_CLOCK				(0x04)	//(0x04<<26)
#define	CSP_DMAC_CFG_WAIT_6_DMA_CLOCK				(0x05)	//(0x05<<26)
#define	CSP_DMAC_CFG_WAIT_7_DMA_CLOCK				(0x06)	//(0x06<<26)
#define	CSP_DMAC_CFG_WAIT_8_DMA_CLOCK				(0x07)	//(0x07<<26)

/* DMA 传输目的端 配置 */
/* DMA 目的端 传输宽度 */
#define	CSP_DMAC_CFG_DEST_DATA_WIDTH_8BIT			(0x00)	//(0x00<<24)
#define	CSP_DMAC_CFG_DEST_DATA_WIDTH_16BIT			(0x01)	//(0x01<<24)
#define	CSP_DMAC_CFG_DEST_DATA_WIDTH_32BIT			(0x02)	//(0x02<<24)

/* DMA 目的端 突发传输模式 */
#define	CSP_DMAC_CFG_DEST_1_BURST       			(0x00)	//(0x00<<23)
#define	CSP_DMAC_CFG_DEST_4_BURST		    		(0x01)	//(0x01<<23)

/* DMA 目的端 地址变化模式 */
#define	CSP_DMAC_CFG_DEST_ADDR_TYPE_LINEAR_MODE		(0x00)	//(0x00<<21)
#define	CSP_DMAC_CFG_DEST_ADDR_TYPE_IO_MODE 		(0x01)	//(0x01<<21)
#define	CSP_DMAC_CFG_DEST_ADDR_TYPE_HPAGE_MODE 		(0x02)	//(0x02<<21)
#define	CSP_DMAC_CFG_DEST_ADDR_TYPE_VPAGE_MODE 		(0x03)	//(0x03<<21)


/* DMA 传输源端 配置 */
/* DMA 源端 传输宽度 */
#define	CSP_DMAC_CFG_SRC_DATA_WIDTH_8BIT			(0x00)	//(0x00<<8)
#define	CSP_DMAC_CFG_SRC_DATA_WIDTH_16BIT			(0x01)	//(0x01<<8)
#define	CSP_DMAC_CFG_SRC_DATA_WIDTH_32BIT			(0x02)	//(0x02<<8)

/* DMA 源端 突发传输模式 */
#define	CSP_DMAC_CFG_SRC_1_BURST       				(0x00)	//(0x00<<7)
#define	CSP_DMAC_CFG_SRC_4_BURST		    		(0x01)	//(0x01<<7)

/* DMA 源端 地址变化模式 */
#define	CSP_DMAC_CFG_SRC_ADDR_TYPE_LINEAR_MODE		(0x00)	//(0x00<<5)
#define	CSP_DMAC_CFG_SRC_ADDR_TYPE_IO_MODE 			(0x01)	//(0x01<<5)
#define	CSP_DMAC_CFG_SRC_ADDR_TYPE_HPAGE_MODE 		(0x02)	//(0x02<<5)
#define	CSP_DMAC_CFG_SRC_ADDR_TYPE_VPAGE_MODE 		(0x03)	//(0x03<<5)


/* DMA 传输目的端 配置 */
/* DMA 传输目的端 N型DMA 目的选择 */
#define	CSP_DMAC_CFG_DEST_TYPE_IR					(0x00)	//(0x00<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_SPDIF		    	(0x01)	//(0x01<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_IIS			    	(0x02)	//(0x02<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_AC97			    	(0x03)	//(0x03<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_SPI0				    (0x04)	//(0x04<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_SPI1				    (0x05)	//(0x05<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_SPI2				    (0x06)	//(0x06<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_UART0				(0x08)	//(0x08<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_UART1				(0x09)	//(0x09<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_UART2				(0x0a)	//(0x0a<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_UART3				(0x0b)	//(0x0b<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_AUDIO_DA				(0x0c)	//(0x0c<<16)

#define	CSP_DMAC_CFG_DEST_TYPE_NFC_DEBUG			(0x0f)	//(0x0f<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_N_SRAM 				(0x10)	//(0x10<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_N_SDRAM				(0x11)	//(0x11<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_UART4				(0x12)	//(0x12<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_UART5				(0x13)	//(0x13<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_UART6				(0x14)	//(0x14<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_UART7				(0x15)	//(0x15<<16)

/* DMA 传输目的端 D型DMA 目的选择 */
#define	CSP_DMAC_CFG_DEST_TYPE_D_SRAM 				(0x00)	//(0x00<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_D_SDRAM				(0x01)	//(0x01<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_TCON0				(0x02)	//(0x02<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_NFC  		    	(0x03)	//(0x03<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_USB0			    	(0x04)	//(0x04<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_USB1			    	(0x05)	//(0x05<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_SDC1			    	(0x07)	//(0x07<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_SDC2 				(0x08)	//(0x08<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_SDC3 				(0x09)	//(0x09<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_MSC  				(0x0a)	//(0x0a<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_EMAC 				(0x0b)	//(0x0b<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_SS   				(0x0d)	//(0x0d<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_USB2			    	(0x0f)	//(0x0f<<16)
#define	CSP_DMAC_CFG_DEST_TYPE_ATA			    	(0x10)	//(0x10<<16)

/* DMA 传输源端 配置 */
/* DMA 传输源端 N型DMA 目的选择 */
#define	CSP_DMAC_CFG_SRC_TYPE_IR					(0x00)	//(0x00<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_SPDIF		    	   	(0x01)	//(0x01<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_IIS			    	(0x02)	//(0x02<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_AC97			    	(0x03)	//(0x03<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_SPI0				    (0x04)	//(0x04<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_SPI1				    (0x05)	//(0x05<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_SPI2				    (0x06)	//(0x06<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_UART0				    (0x08)	//(0x08<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_UART1				    (0x09)	//(0x09<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_UART2				    (0x0a)	//(0x0a<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_UART3				    (0x0b)	//(0x0b<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_AUDIO 				(0x0c)	//(0x0c<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_TP     				(0x0d)	//(0x0d<<0)

#define	CSP_DMAC_CFG_SRC_TYPE_NFC_DEBUG			    (0x0f)	//(0x0f<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_N_SRAM 				(0x10)	//(0x10<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_N_SDRAM				(0x11)	//(0x11<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_UART4				    (0x12)	//(0x12<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_UART5				    (0x13)	//(0x13<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_UART6				    (0x14)	//(0x14<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_UART7				    (0x15)	//(0x15<<0)

/* DMA 传输源端 D型DMA 目的选择 */
#define	CSP_DMAC_CFG_SRC_TYPE_D_SRAM 				(0x00)	//(0x00<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_D_SDRAM				(0x01)	//(0x01<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_TCON0				    (0x02)	//(0x02<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_NFC  		    	   	(0x03)	//(0x03<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_USB0			    	(0x04)	//(0x04<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_USB1			    	(0x05)	//(0x05<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_SDC1			    	(0x07)	//(0x07<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_SDC2 				    (0x08)	//(0x08<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_SDC3 				    (0x09)	//(0x09<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_MSC  				    (0x0a)	//(0x0a<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_EMAC 				    (0x0c)	//(0x0c<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_SS   				    (0x0e)	//(0x0e<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_USB2			    	(0x0f)	//(0x0f<<0)
#define	CSP_DMAC_CFG_SRC_TYPE_ATA			    	(0x10)	//(0x10<<0)




typedef struct  CSP_dma_config
{
    unsigned int      src_drq_type     ; //源地址存储类型，如DRAM, SPI,NAND等，根据选择NDMA或者DDMA, 选择 __ndma_drq_type_t或者 __ddma_src_type_t
    unsigned int      src_addr_type    ; //原地址类型 NDMA下 0:递增模式  1:保持不变  DDMA下 0:递增模式  1:保持不变  2:H模式  3:V模式
    unsigned int      src_burst_length ; //发起一次burst宽度 填0对应于1，填1对应于4,
    unsigned int      src_data_width   ; //数据传输宽度，0:一次传输8bit，1:一次传输16bit，2:一次传输32bit，3:保留
    unsigned int      dst_drq_type     ; //源地址存储类型，如DRAM, SPI,NAND等，根据选择NDMA或者DDMA, 选择 __ndma_drq_type_t或者 __ddma_dst_type_t
    unsigned int      dst_addr_type    ; //原地址类型 NDMA下 0:递增模式  1:保持不变  DDMA下 0:递增模式  1:保持不变  2:H模式  3:V模式
    unsigned int      dst_burst_length ; //发起一次burst宽度 填0对应于1，填1对应于4,
    unsigned int      dst_data_width   ; //数据传输宽度，0:一次传输8bit，1:一次传输16bit，2:一次传输32bit，3:保留
    unsigned int      wait_state       ; //等待时钟个数 选择范围从0-7，只对NDMA有效
    unsigned int      continuous_mode  ; //选择连续工作模式 0:传输一次即结束 1:反复传输，当一次DMA传输结束后，重新开始传输

    unsigned int      cmt_blk_cnt	   ; //DMA传输comity counter
}CSP_dma_config_t;




#endif	//__CSP_DMAC_PARA_H__
