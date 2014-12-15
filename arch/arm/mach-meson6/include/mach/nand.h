
#ifndef NAND_H_INCLUDED
#define NAND_H_INCLUDED
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include "am_regs.h"
#include <linux/earlysuspend.h>
#include <mach/pinmux.h>
#include <linux/pinctrl/consumer.h>
#define MX_REVD
#ifdef CONFIG_CLK81_DFS
#include <linux/semaphore.h>
#define init_MUTEX(sem)             sema_init(sem, 1)
#endif

#ifdef MX_REVD
#define	NAND_PAGELIST_MAGIC 	0x4c50414e
#endif
/** Register defination **/
#define IO_CBUS_BASE2  0xc1100000

#define NAND_CMD  ((0xc1108600-IO_CBUS_BASE2)>>2)
#define NAND_CFG  ((0xc1108604-IO_CBUS_BASE2)>>2)
#define NAND_DADR ((0xc1108608-IO_CBUS_BASE2)>>2)
#define NAND_IADR ((0xc110860c-IO_CBUS_BASE2)>>2)
#define NAND_BUF  ((0xc1108610-IO_CBUS_BASE2)>>2)
#define NAND_INFO ((0xc1108614-IO_CBUS_BASE2)>>2)
#define NAND_DC   ((0xc1108618-IO_CBUS_BASE2)>>2)
#define NAND_ADR  ((0xc110861c-IO_CBUS_BASE2)>>2)
#define NAND_DL   ((0xc1108620-IO_CBUS_BASE2)>>2)
#define NAND_DH   ((0xc1108624-IO_CBUS_BASE2)>>2)
#define NAND_CADR ((0xc1108628-IO_CBUS_BASE2)>>2)
#define NAND_SADR ((0xc110862c-IO_CBUS_BASE2)>>2)

#define P_NAND_CMD                                CBUS_REG_ADDR(NAND_CMD)
#define P_NAND_CFG                                CBUS_REG_ADDR(NAND_CFG)
#define P_NAND_DADR                               CBUS_REG_ADDR(NAND_DADR)
#define P_NAND_IADR                               CBUS_REG_ADDR(NAND_IADR)
#define P_NAND_BUF                                CBUS_REG_ADDR(NAND_BUF)
#define P_NAND_INFO                               CBUS_REG_ADDR(NAND_INFO)
#define P_NAND_DC                                 CBUS_REG_ADDR(NAND_DC)
#define P_NAND_ADR                                CBUS_REG_ADDR(NAND_ADR)
#define P_NAND_DL                                 CBUS_REG_ADDR(NAND_DL)
#define P_NAND_DH                                 CBUS_REG_ADDR(NAND_DH)
#define P_NAND_CADR                               CBUS_REG_ADDR(NAND_CADR)
#define P_NAND_SADR                               CBUS_REG_ADDR(NAND_SADR)

#define NAND_SYS_CLK_NAME	  "clk81"
#define NAND_CYCLE_DELAY	  90
#define NAND_BOOT_NAME		  "bootloader"
#define NAND_NORMAL_NAME	  "nandnormal"
#define NAND_MULTI_NAME		  "nandmulti"

#define NAND_CONVERST_ADDR	  		0xa0000000
#define M3_BOOT_WRITE_SIZE	  		0x600
#define M3_BOOT_COPY_NUM	  		4
#define M3_BOOT_PAGES_PER_COPY	 	256

#define NFC_BASE			  CBUS_REG_ADDR(NAND_CMD)
#define NFC_OFF_CMD           ((NAND_CMD -NAND_CMD)<<2)
#define NFC_OFF_CFG           ((NAND_CFG -NAND_CMD)<<2)
#define NFC_OFF_DADR          ((NAND_DADR-NAND_CMD)<<2)
#define NFC_OFF_IADR          ((NAND_IADR-NAND_CMD)<<2)
#define NFC_OFF_BUF           ((NAND_BUF -NAND_CMD)<<2)
#define NFC_OFF_INFO          ((NAND_INFO-NAND_CMD)<<2)
#define NFC_OFF_DC            ((NAND_DC  -NAND_CMD)<<2)
#define NFC_OFF_ADR           ((NAND_ADR -NAND_CMD)<<2)
#define NFC_OFF_DL            ((NAND_DL  -NAND_CMD)<<2)
#define NFC_OFF_DH            ((NAND_DH  -NAND_CMD)<<2)

/*
   Common Nand Read Flow
*/
#define CE0         (0xe<<10)
#define CE1         (0xd<<10)
#define CE2         (0xb<<10)
#define CE3         (0x7<<10)
#define CE_NOT_SEL  (0xf<<10)
#define IO4 ((0xe<<10)|(1<<18)) 
#define IO5 ((0xd<<10)|(1<<18)) 
#define IO6 ((0xb<<10)|(1<<18)) 
#define CLE         (0x5<<14)
#define ALE         (0x6<<14)
#define DWR         (0x4<<14)
#define DRD         (0x8<<14)
#define IDLE        (0xc<<14)
#define RB  		(1<<20) 
#define STANDBY     (0xf<<10)

#define MAX_CYCLE_NUM		20
#define PER_INFO_BYTE 		8
#define SIZE_INT	  		(sizeof(unsigned int))


#define M2N  ((0<<17) | (2<<20) | (1<<19))
#define N2M  ((1<<17) | (2<<20) | (1<<19))

#define M2N_NORAN  0x00200000
#define N2M_NORAN  0x00220000

#define STS  ((3<<17) | (2<<20))
#define ADL  ((0<<16) | (3<<20))
#define ADH  ((1<<16) | (3<<20))
#define AIL  ((2<<16) | (3<<20))
#define AIH  ((3<<16) | (3<<20))
#define ASL  ((4<<16) | (3<<20))
#define ASH  ((5<<16) | (3<<20))
#define SEED ((8<<16) | (3<<20))

/**
    Nand Flash Controller (M1)
    Global Macros
*/
/**
   Config Group
*/
#define NFC_SET_TIMING(mode,cycles,adjust)    WRITE_CBUS_REG_BITS(NAND_CFG,((cycles)|((adjust&0xf)<<10)|((mode&7)<<5)),0,14)
#define NFC_SET_CMD_START()						   		SET_CBUS_REG_MASK(NAND_CFG,1<<12) 
#define NFC_SET_CMD_AUTO()						   		SET_CBUS_REG_MASK(NAND_CFG,1<<13) 
#define NFC_SET_STS_IRQ(en)					       		WRITE_CBUS_REG_BITS(NAND_CFG,en,20,1) 
#define NFC_SET_CMD_IRQ(en)					       		WRITE_CBUS_REG_BITS(NAND_CFG,en,21,1) 
#define NFC_SET_TIMING_ASYC(bus_tim,bus_cyc)       		WRITE_CBUS_REG_BITS(NAND_CFG,((bus_cyc&31)|((bus_tim&31)<<5)|(0<<10)),0,12)
#define NFC_SET_TIMING_SYNC(bus_tim,bus_cyc,sync_mode)  WRITE_CBUS_REG_BITS(NAND_CFG,(bus_cyc&31)|((bus_tim&31)<<5)|((sync_mode&2)<<10),0,12)
#define NFC_SET_TIMING_SYNC_ADJUST() 
#define NFC_SET_DMA_MODE(is_apb,spare_only)        WRITE_CBUS_REG_BITS(NAND_CFG,((spare_only<<1)|(is_apb)),14,2)
#define NFC_ENABLE_TOSHIBA_TOGGLE_MODE()       	SET_CBUS_REG_MASK(NAND_CFG,1<<11)
#define NFC_EXIT_TOSHIBA_TOGGLE_MODE() 			CLEAR_CBUS_REG_MASK(NAND_CFG,1<<11)
#define NFC_ENABLE_MICRON_TOGGLE_MODE()      		 SET_CBUS_REG_MASK(NAND_CFG,1<<10)
#define NFC_SYNC_ADJ()      							SET_CBUS_REG_MASK(NAND_CFG,1<<16)
#define NFC_EXIT_SYNC_ADJ()      							CLEAR_CBUS_REG_MASK(NAND_CFG,1<<16)
/**
    CMD relative Macros
    Shortage word . NFCC
*/
#define NFC_CMD_IDLE(ce,time)          ((ce)|IDLE|(time&0x3ff))
#define NFC_CMD_CLE(ce,cmd  )          ((ce)|CLE |(cmd &0x0ff))
#define NFC_CMD_ALE(ce,addr )          ((ce)|ALE |(addr&0x0ff))
#define NFC_CMD_STANDBY(time)          (STANDBY  |(time&0x3ff))
#define NFC_CMD_ADL(addr)              (ADL     |(addr&0xffff))
#define NFC_CMD_ADH(addr)              (ADH|((addr>>16)&0xffff))
#define NFC_CMD_AIL(addr)              (AIL     |(addr&0xffff))
#define NFC_CMD_AIH(addr)              (AIH|((addr>>16)&0xffff))
#define NFC_CMD_DWR(data)              (DWR     |(data&0xff  ))
#define NFC_CMD_DRD(ce,size)           (ce|DRD|size)
#define NFC_CMD_RB(ce,time  )          ((ce)|RB  |(time&0x1f))
#define NFC_CMD_RB_INT(ce,time)        ((ce)|RB|(((ce>>10)^0xf)<<14)|(time&0x1f))
#define NFC_CMD_RBIO(time,io)		   (RB|io|(time&0x1f))	
#define NFC_CMD_RBIO_INT(io,time)      (RB|(((io>>10)^0x7)<<14)|(time&0x1f))
#define NFC_CMD_SEED(seed)			   (SEED|(0xc2 + (seed&0x7fff)))
#define NFC_CMD_STS(tim) 			   (STS|(tim&3))
#define NFC_CMD_M2N(ran,ecc,sho,pgsz,pag)      ((ran?M2N:M2N_NORAN)|(ecc<<14)|(sho<<13)|((pgsz&0x7f)<<6)|(pag&0x3f))
#define NFC_CMD_N2M(ran,ecc,sho,pgsz,pag)      ((ran?N2M:N2M_NORAN)|(ecc<<14)|(sho<<13)|((pgsz&0x7f)<<6)|(pag&0x3f))




/**
    Alias for CMD
*/
#define NFC_CMD_D_ADR(addr)         NFC_CMD_ADL(addr),NFC_CMD_ADH(addr)   
#define NFC_CMD_I_ADR(addr)         NFC_CMD_ADI(addr),NFC_CMD_ADI(addr)   


/**
    Register Operation and Controller Status 
*/
#define NFC_SEND_CMD(cmd)           (WRITE_CBUS_REG(NAND_CMD,cmd))
#define NFC_READ_INFO()             (READ_CBUS_REG(NAND_CMD))
/** ECC defination(M1) */
#define NAND_ECC_NONE             (0x0<<14)
#define NAND_ECC_REV0             (0x1<<14)
#define NAND_ECC_REV1             (0x2<<14)
#define NAND_ECC_REV2             (0x3<<14)
#define NAND_ECC_BCH9             (0x4<<14)
#define NAND_ECC_BCH12            (0x6<<14)
#define NAND_ECC_BCH16            (0x7<<14)

#define NAND_ECC_BCH8             (0x1)
#define NAND_ECC_BCH8_1K          (0x2)
#define NAND_ECC_BCH16_1K         (0x3)
#define NAND_ECC_BCH24_1K         (0x4)
#define NAND_ECC_BCH30_1K 		  (0x5)
#define NAND_ECC_BCH40_1K 		  (0x6)
#define NAND_ECC_BCH60_1K 		  (0x7)
#define NAND_ECC_BCH_SHORT		  (0x8)
/**
    Cmd FIFO control
*/
#define NFC_CMD_FIFO_GO()               (WRITE_CBUS_REG(NAND_CMD,(1<<30)))
#define NFC_CMD_FIFO_RESET()            (WRITE_CBUS_REG(NAND_CMD,(1<<31)))
/**
    ADDR operations
*/
#define NFC_SET_DADDR(a)         (WRITE_CBUS_REG(NAND_DADR,(unsigned)a))
#define NFC_SET_IADDR(a)         (WRITE_CBUS_REG(NAND_IADR,(unsigned)a))
#define NFC_SET_SADDR(a)		 (WRITE_CBUS_REG(NAND_SADR,(unsigned)a))	

/**
    Send command directly
*/
/*#define NFC_SEND_CMD_IDLE(ce,time)          NFC_SEND_CMD((ce)|IDLE|(time&0x3ff))
#define NFC_SEND_CMD_CLE(ce,cmd  )          NFC_SEND_CMD((ce)|CLE |(cmd &0x0ff))
#define NFC_SEND_CMD_ALE(ce,addr )          NFC_SEND_CMD((ce)|ALE |(addr&0x0ff))
#define NFC_SEND_CMD_RB(ce,time  )          NFC_SEND_CMD((ce)|RB  |(time&0x3ff))
#define NFC_SEND_CMD_STANDBY(time)          NFC_SEND_CMD(STANDBY  |(time&0x3ff))
#define NFC_SEND_CMD_ADL(addr)              NFC_SEND_CMD(ADL     |(addr&0xffff))
#define NFC_SEND_CMD_ADH(addr)              NFC_SEND_CMD(ADH|((addr>>16)&0xffff))
#define NFC_SEND_CMD_AIL(addr)              NFC_SEND_CMD(AIL     |(addr&0xffff))
#define NFC_SEND_CMD_AIH(addr)              NFC_SEND_CMD(AIH|((addr>>16)&0xffff))
#define NFC_SEND_CMD_M2N(size,ecc)          NFC_SEND_CMD(M2N |ecc|(size&0x3fff))
#define NFC_SEND_CMD_N2M(size,ecc)          NFC_SEND_CMD(N2M |ecc|(size&0x3fff))
#define NFC_SEND_CMD_DWR(data)              NFC_SEND_CMD(DWR     |(data&0xff  ))
#define NFC_SEND_CMD_DRD(    )              NFC_SEND_CMD(DRD                   )
*/
#define NFC_SEND_CMD_IDLE(ce,time)          {while(NFC_CMDFIFO_SIZE()>0);NFC_SEND_CMD(NFC_CMD_IDLE(ce,time));}
#define NFC_SEND_CMD_CLE(ce,cmd  )          NFC_SEND_CMD(NFC_CMD_CLE(ce,cmd))
#define NFC_SEND_CMD_ALE(ce,addr )          NFC_SEND_CMD(NFC_CMD_ALE(ce,addr))
#define NFC_SEND_CMD_STANDBY(time)          NFC_SEND_CMD(NFC_CMD_STANDBY(time))
#define NFC_SEND_CMD_ADL(addr)              NFC_SEND_CMD(NFC_CMD_ADL(addr))
#define NFC_SEND_CMD_ADH(addr)              NFC_SEND_CMD(NFC_CMD_ADH(addr))
#define NFC_SEND_CMD_AIL(addr)              NFC_SEND_CMD(NFC_CMD_AIL(addr))
#define NFC_SEND_CMD_AIH(addr)              NFC_SEND_CMD(NFC_CMD_AIH(addr))
#define NFC_SEND_CMD_DWR(data)              NFC_SEND_CMD(NFC_CMD_DWR(data))
#define NFC_SEND_CMD_DRD(ce,size)           NFC_SEND_CMD(NFC_CMD_DRD(ce,size))
#define NFC_SEND_CMD_RB(ce,time)          	NFC_SEND_CMD(NFC_CMD_RB(ce,time))
#define NFC_SEND_CMD_SEED(seed)				NFC_SEND_CMD(NFC_CMD_SEED(seed))	
#define NFC_SEND_CMD_M2N(ran,ecc,sho,pgsz,pag)   NFC_SEND_CMD(NFC_CMD_M2N(ran,ecc,sho,pgsz,pag))
#define NFC_SEND_CMD_N2M(ran,ecc,sho,pgsz,pag)   NFC_SEND_CMD(NFC_CMD_N2M(ran,ecc,sho,pgsz,pag))

#define NFC_SEND_CMD_M2N_RAW(ran,len)	NFC_SEND_CMD((ran?M2N:M2N_NORAN)|(len&0x3fff))
#define NFC_SEND_CMD_N2M_RAW(ran,len)   NFC_SEND_CMD((ran?N2M:N2M_NORAN)|(len&0x3fff))


/**
    Cmd Info Macros
*/
#define NFC_INFO_GET()                      (READ_CBUS_REG(NAND_CMD))
#define NFC_CMDFIFO_SIZE()                  ((NFC_INFO_GET()>>22)&0x1f)
#define NFC_CHECEK_RB_TIMEOUT()             ((NFC_INFO_GET()>>27)&0x1)
#define NFC_GET_RB_STATUS(ce)               (((NFC_INFO_GET()>>28)&(~(ce>>10)))&0xf)
#define NFC_GET_BUF() 					    READ_CBUS_REG(NAND_BUF)
#define NFC_SET_CFG(val) 			      	(WRITE_CBUS_REG(NAND_CFG,(unsigned)val))	
#define NFC_FIFO_CUR_CMD()				    ((NFC_INFO_GET()>>22)&0x3FFFFF)		


#define NAND_INFO_DONE(a)         (((a)>>31)&1)
#define NAND_ECC_ENABLE(a)        (((a)>>30)&1)
#define NAND_ECC_CNT(a)           (((a)>>24)&0x3f)
#define NAND_ZERO_CNT(a)	      (((a)>>16)&0x3f)	
#define NAND_INFO_DATA_2INFO(a)   ((a)&0xffff)
#define NAND_INFO_DATA_1INFO(a)   ((a)&0xff)

#define NAND_DEFAULT_OPTIONS			(NAND_TIMING_MODE5 | NAND_ECC_BCH8_MODE)

#define AML_NORMAL						0
#define AML_MULTI_CHIP					1
#define AML_MULTI_CHIP_SHARE_RB			2
#define AML_CHIP_NONE_RB				4
#define AML_INTERLEAVING_MODE			8

#define AML_NAND_CE0         			0xe
#define AML_NAND_CE1         			0xd
#define AML_NAND_CE2         			0xb
#define AML_NAND_CE3         			0x7

#define AML_BADBLK_POS					0
#define NAND_ECC_UNIT_SIZE				512
#define NAND_ECC_UNIT_1KSIZE			1024
#define NAND_ECC_UNIT_SHORT			    384

#define NAND_BCH9_ECC_SIZE				15
#define NAND_BCH8_ECC_SIZE				14
#define NAND_BCH12_ECC_SIZE				20
#define NAND_BCH16_ECC_SIZE				26
#define NAND_BCH8_1K_ECC_SIZE			14
#define NAND_BCH16_1K_ECC_SIZE			28
#define NAND_BCH24_1K_ECC_SIZE			42
#define NAND_BCH30_1K_ECC_SIZE			54
#define NAND_BCH40_1K_ECC_SIZE			70
#define NAND_BCH60_1K_ECC_SIZE			106

#define NAND_ECC_OPTIONS_MASK			0x0000000f
#define NAND_PLANE_OPTIONS_MASK			0x000000f0
#define NAND_TIMING_OPTIONS_MASK		0x00000f00
#define NAND_BUSW_OPTIONS_MASK			0x0000f000
#define NAND_INTERLEAVING_OPTIONS_MASK	0x000f0000
#define NAND_ECC_SOFT_MODE				0x00000000
#define NAND_ECC_SHORT_MODE				0x00000001
#define NAND_ECC_BCH9_MODE				0x00000002
#define NAND_ECC_BCH8_MODE				0x00000003
#define NAND_ECC_BCH12_MODE				0x00000004
#define NAND_ECC_BCH16_MODE				0x00000005
#define NAND_ECC_BCH8_1K_MODE			0x00000006
#define NAND_ECC_BCH16_1K_MODE			0x00000007
#define NAND_ECC_BCH24_1K_MODE			0x00000008
#define NAND_ECC_BCH30_1K_MODE			0x00000009
#define NAND_ECC_BCH40_1K_MODE			0x0000000a
#define NAND_ECC_BCH60_1K_MODE			0x0000000b
#define NAND_TWO_PLANE_MODE				0x00000010
#define NAND_TIMING_MODE0				0x00000000
#define NAND_TIMING_MODE1				0x00000100
#define NAND_TIMING_MODE2				0x00000200
#define NAND_TIMING_MODE3				0x00000300
#define NAND_TIMING_MODE4				0x00000400
#define NAND_TIMING_MODE5				0x00000500
#define NAND_INTERLEAVING_MODE			0x00010000

#define DEFAULT_T_REA					40
#define DEFAULT_T_RHOH					0

#define AML_NAND_BUSY_TIMEOUT			0x40000
#define AML_DMA_BUSY_TIMEOUT			0x100000
#define MAX_ID_LEN						8

#define NAND_CMD_PLANE2_READ_START		0x06
#define NAND_CMD_TWOPLANE_PREVIOS_READ	0x60
#define NAND_CMD_TWOPLANE_READ1			0x5a
#define NAND_CMD_TWOPLANE_READ2			0xa5
#define NAND_CMD_TWOPLANE_WRITE2_MICRO	0x80
#define NAND_CMD_TWOPLANE_WRITE2		0x81
#define NAND_CMD_DUMMY_PROGRAM			0x11
#define NAND_CMD_ERASE1_END				0xd1
#define NAND_CMD_MULTI_CHIP_STATUS		0x78
#define NAND_CMD_SET_FEATURES			0xEF
#define NAND_CMD_GET_FEATURES			0xEE
#define ONFI_TIMING_ADDR				0x01

#define MAX_CHIP_NUM		4
#define USER_BYTE_NUM		4

#define NAND_STATUS_READY_MULTI			0x20

#define NAND_BLOCK_GOOD					0
#define NAND_BLOCK_BAD					1
#define NAND_FACTORY_BAD				2
#define	FACTORY_BAD_BLOCK_ERROR  	159
#define NAND_MINI_PART_SIZE				0x800000
#define NAND_MINI_PART_BLOCKNUM			2
#define NAND_MINI_PART_NUM				4
#define MAX_BAD_BLK_NUM					2000
#define MAX_MTD_PART_NUM				16
#define MAX_MTD_PART_NAME_LEN			24
#define ENV_NAND_MAGIC					"envx"
#define BBT_HEAD_MAGIC					"bbts"
#define BBT_TAIL_MAGIC					"bbte"
#define MTD_PART_MAGIC					"anpt"

#define CONFIG_ENV_SIZE         		0x8000

#define ENV_SIZE (CONFIG_ENV_SIZE - (sizeof(uint32_t)))
#define NAND_SYS_PART_SIZE				0x10000000

#define ENV_NAND_SCAN_BLK                            50

#define NAND_MINIKEY_PART_SIZE				0x800000
#define NAND_MINIKEY_PART_NUM				4
//#define NAND_MINIKEY_PART_BLOCKNUM			CONFIG_NAND_KEY_BLOCK_NUM
#define NAND_MINIKEY_PART_BLOCKNUM			4
//#define CONFIG_KEYSIZE         		(0x4000*1)
#define CONFIG_KEYSIZE         		(0x4000*4)
#define ENV_KEY_MAGIC					"keyx"

#define NAND_KEY_SAVE_MULTI_BLOCK  //key save in multi block same time

struct aml_nand_flash_dev {
	char *name;
	u8 id[MAX_ID_LEN];
	unsigned pagesize;
	unsigned chipsize;
	unsigned erasesize;
	unsigned oobsize;
	unsigned internal_chipnr;
	unsigned T_REA;
	unsigned T_RHOH;
	u8 onfi_mode;
	unsigned options;
};

struct aml_nand_part_info {
	char mtd_part_magic[4];
	char mtd_part_name[MAX_MTD_PART_NAME_LEN];
	uint64_t size;
	uint64_t offset;
	u_int32_t mask_flags;
};

struct aml_nand_bbt_info {
	char bbt_head_magic[4];
	int16_t nand_bbt[MAX_BAD_BLK_NUM];
	struct aml_nand_part_info aml_nand_part[MAX_MTD_PART_NUM];
	char bbt_tail_magic[4];
};
struct aml_nandkey_info_t {
         struct mtd_info *mtd;
         struct env_valid_node_t *env_valid_node;
         struct env_free_node_t *env_free_node;
         u_char env_valid;
         u_char env_init;
         u_char part_num_before_sys;
         struct aml_nand_bbt_info nand_bbt_info;
         int start_block;
         int end_block;
};

struct env_valid_node_t {
	int16_t  ec;
	int16_t	phy_blk_addr;
	int16_t	phy_page_addr;
	int timestamp;
#ifdef NAND_KEY_SAVE_MULTI_BLOCK
	int rd_flag;
	struct env_valid_node_t *next;
#endif
};

struct env_free_node_t {
	int16_t  ec;
	int16_t	phy_blk_addr;
	int dirty_flag;
	struct env_free_node_t *next;
};

struct env_oobinfo_t {
	char name[4];
    int16_t  ec;
    unsigned        timestamp: 15;
    unsigned       status_page: 1;
};

struct aml_nandenv_info_t {
	struct mtd_info *mtd;
	struct env_valid_node_t *env_valid_node;
	struct env_free_node_t *env_free_node;
	u_char env_valid;
	u_char env_init;
	u_char part_num_before_sys;
	struct aml_nand_bbt_info nand_bbt_info;
};

typedef	struct environment_s {
	uint32_t	crc;		/* CRC32 over data bytes	*/
	unsigned char	data[ENV_SIZE]; /* Environment data		*/
} env_t;

struct aml_nand_bch_desc{
    char * name;
    unsigned bch_mode;
    unsigned bch_unit_size;
    unsigned bch_bytes;
    unsigned user_byte_mode;
};

#define PROC_CONFIG_INFO	0xD901FF00
#define POR_GET_1ST_CFG(a) (a&7)
#define POR_1ST_NAND 7
#define POR_1ST_NAND_RB 6
#define POR_1ST_SPI 5
#define POR_1ST_SPI_RESERVED 4
#define POR_1ST_SDIO_C 3
#define POR_1ST_SDIO_B 2
#define POR_1ST_SDIO_A 1
#define POR_1ST_NEVER_CHECKED 0
#define POR_GET_2ND_CFG(a) ((a>>3)&3)
#define POR_2ND_SDIO_B 3
#define POR_2ND_SDIO_A 2
#define POR_2ND_SDIO_C 1
#define POR_2ND_NEVER_CHECKED 0
#define POR_GET_CRYSTAL(a) ((a>>7)&1)
#define POR_CRYSTAL_24M 1
#define POR_CRYSTAL_25M 0
#define POR_GET_USB_CFG(a) ((a>>8)&1)
#define POR_USB_ENABLE 1
#define POR_USB_DISABLE 0

#define	NEW_NAND_SUPPORT	

#ifdef NEW_NAND_SUPPORT
#define RETRY_NAND_MAGIC		"refv"
#define RETRY_NAND_BLK_NUM		2
#define RETRY_NAND_COPY_NUM	4

#define	READ_RETRY_REG_NUM   	8
#define	READ_RETRY_CNT   		30
#define	HYNIX_RETRY_CNT		20

#define	ENHANCE_SLC_REG_NUM   	5

#define	READ_RETRY_ZERO   		((char)-1)
#define	NAND_CMD_HYNIX_GET_VALUE				0x37
#define	NAND_CMD_HYNIX_SET_VALUE_START		0x36
#define	NAND_CMD_HYNIX_SET_VALUE_END		0x16

#define	NAND_CMD_TOSHIBA_PRE_CON1			0x5c
#define	NAND_CMD_TOSHIBA_PRE_CON2			0xc5
#define	NAND_CMD_TOSHIBA_SET_VALUE			0x55
#define	NAND_CMD_TOSHIBA_BEF_COMMAND1		0x26
#define	NAND_CMD_TOSHIBA_BEF_COMMAND2		0x5d
#define      NAND_CMD_SAMSUNG_SET_VALUE			0XA1
#define      NAND_CMD_MICRON_SET_VALUE                       0XEF

//for Hynix
#define	HYNIX_26NM_8GB 		1		//H27UCG8T2M
#define	HYNIX_26NM_4GB 		2		//H27UBG8T2BTR
#define	HYNIX_20NM_8GB 		3		//
#define	HYNIX_20NM_4GB 		4		//
#define	HYNIX_20NM_LGA_8GB 		5		//
#define	HYNIX_1YNM_8GB 		6
//for Toshiba
#define	TOSHIBA_24NM 			20		//TC58NVG5D2HTA00
#define	TOSHIBA_A19NM 			21	
										//TC58NVG6D2GTA00
//for SAMSUNG
#define	SUMSUNG_2XNM 			30	

#define   MICRON_20NM			40

//for SANDISK
#define    SANDISK_19NM			50
#define     SANDISK_24NM			51
#define     SANDISK_A19NM		52

#define      DYNAMIC_REG_NUM        3
#define      DYNAMIC_REG_INIT_NUM        9
#define      DYNAMIC_READ_CNT_LOWER       15
#define      DYNAMIC_READ_CNT_UPPER       19
#define      DYNAMIC_CNT_LOWER       16
#define      DYNAMIC_CNT_UPPER       20


#define      DYNAMIC_READ_CASE_NUM        20


#define	NAND_CMD_SANDISK_INIT_ONE				0x3B
#define	NAND_CMD_SANDISK_INIT_TWO				0xB9

#define	NAND_CMD_SANDISK_DSP_ON					0x26
#define	NAND_CMD_SANDISK_RETRY_STA					 0x5D
#define	NAND_CMD_SANDISK_LOAD_VALUE_ONE			0x53
#define	NAND_CMD_SANDISK_LOAD_VALUE_TWO			0x54

#define	NAND_CMD_SANDISK_DYNAMIC_ENABLE			0xB6
#define	NAND_CMD_SANDISK_DYNAMIC_DISABLE			0xD6
#define 	NAND_CMD_SANDISK_SLC  						0xA2     
#define   NAND_CMD_SANDISK_SET_VALUE					0XEF
#define   NAND_CMD_SANDISK_GET_VALUE					0XEE
#define	NAND_CMD_SANDISK_SET_OUTPUT_DRV			0x10
#define	NAND_CMD_SANDISK_SET_VENDOR_SPC			0x80

#define	NAND_CMD_MICRON_SET_TOGGLE_SPC			0x01




struct aml_nand_read_retry{
	u8	flag;
	u8	reg_cnt;
	u8	retry_cnt;
	u8	default_flag;
	u8	cur_cnt[MAX_CHIP_NUM];
	u8	reg_addr[READ_RETRY_REG_NUM];
	u8	reg_default_value[MAX_CHIP_NUM][READ_RETRY_REG_NUM];	
	char	reg_offset_value[MAX_CHIP_NUM][READ_RETRY_CNT][READ_RETRY_REG_NUM];	
	void	(*get_default_value)(struct mtd_info *mtd);
	void	(*set_default_value)(struct mtd_info *mtd);
	void	(*save_default_value)(struct mtd_info *mtd);
	void	(*read_retry_handle)(struct mtd_info *mtd, int chipnr);
	void	(*read_retry_exit)(struct mtd_info *mtd, int chipnr);
};

struct aml_nand_slc_program{
	u8	flag;
	u8	reg_cnt;
	u8	reg_addr[ENHANCE_SLC_REG_NUM];
	u8	reg_default_value[MAX_CHIP_NUM][ENHANCE_SLC_REG_NUM];	
	char	reg_offset_value[ENHANCE_SLC_REG_NUM];
	void	(*get_default_value)(struct mtd_info *mtd);
	void	(*exit_enslc_mode)(struct mtd_info *mtd);
	void	(*enter_enslc_mode)(struct mtd_info *mtd);
};

// this for sandisk dynamic read
struct aml_nand_dynamic_read{
	u8 slc_flag;
	u8 dynamic_read_flag;
	u8 read_case_num_max_lower_page;//Nmax	_lower_page
	u8 read_case_num_max_upper_page;//Nmax_upper_page	
	u8 cur_case_num_lower_page[MAX_CHIP_NUM];//N_lower_page	
	u8 cur_case_num_upper_page[MAX_CHIP_NUM];//N_upper_page
	u8	reg_addr_init[DYNAMIC_REG_INIT_NUM];
	u8	reg_addr_lower_page[DYNAMIC_REG_NUM];	
	u8	reg_addr_upper_page[DYNAMIC_REG_NUM];	
	char	reg_offset_value_lower_page[DYNAMIC_CNT_LOWER][DYNAMIC_REG_NUM];		
	char	reg_offset_value_upper_page[DYNAMIC_CNT_UPPER][DYNAMIC_REG_NUM];	
	void	(*dynamic_read_init)(struct mtd_info *mtd);
	void	(*dynamic_read_handle)(struct mtd_info *mtd, int page, int chipnr);
	void	(*dynamic_read_exit)(struct mtd_info *mtd, int chipnr);
	void	(*exit_slc_mode)(struct mtd_info *mtd);
	void	(*enter_slc_mode)(struct mtd_info *mtd);
};

struct new_tech_nand_t{
    u8	type;
    struct aml_nand_slc_program slc_program_info;
    struct aml_nand_read_retry read_rety_info;
    struct aml_nand_dynamic_read dynamic_read_info;
};
#endif


struct aml_nand_chip {

	u8 mfr_type;
	unsigned onfi_mode;
	unsigned T_REA;
	unsigned T_RHOH;
	unsigned options;
	unsigned page_size;
	unsigned block_size;
	unsigned oob_size;
	unsigned virtual_page_size;
	unsigned virtual_block_size;
	u8 plane_num;
	u8 chip_num;
	u8 internal_chipnr;
	unsigned internal_page_nums;

	unsigned int update_env_flag;
	unsigned int		  ran_mode; 			   //def close, for all part
	unsigned int		  rbpin_mode;				   //may get from romboot 
	unsigned int		  rbpin_detect;				   //add for rbpin auto detect
	unsigned int		  short_pgsz;			   //zero means no short 
	unsigned internal_chip_shift;
	unsigned bch_mode;
	u8 user_byte_mode;
	u8 ops_mode;
	u8 cached_prog_status;
	u8 max_bch_mode;
	unsigned chip_enable[MAX_CHIP_NUM];
	unsigned rb_enable[MAX_CHIP_NUM];
	unsigned chip_selected;
	unsigned rb_received;
	unsigned valid_chip[MAX_CHIP_NUM];
	unsigned page_addr;
	unsigned char *aml_nand_data_buf;
	dma_addr_t data_dma_addr;
	unsigned int *user_info_buf;
	dma_addr_t nand_info_dma_addr;
	int8_t *block_status;
#ifdef CONFIG_CLK81_DFS
    struct semaphore nand_sem;
    int lock_state;
#endif
	u8 ecc_cnt_limit;
	u8 ecc_cnt_cur;
	u8 ecc_max;
	unsigned int 		 toggle_mode;
    unsigned zero_cnt;
	unsigned oob_fill_cnt;
	struct mtd_info			mtd;
	struct nand_chip		chip;
	struct aml_nandenv_info_t *aml_nandenv_info;
	struct aml_nand_bch_desc 	*bch_desc;
#ifdef NEW_NAND_SUPPORT
	struct new_tech_nand_t  new_nand_info;
#endif
	/* platform info */
	struct aml_nand_platform	*platform;

	/* device info */
	struct device			*device;

	/* nand env device */
	struct cdev				nand_env_cdev;
	

	
	struct aml_nandkey_info_t *aml_nandkey_info;
	struct cdev				nand_key_cdev;

	struct early_suspend nand_early_suspend;
    struct class      cls;

	//plateform operation function
	void	(*aml_nand_hw_init)(struct aml_nand_chip *aml_chip);
	void	(*aml_nand_adjust_timing)(struct aml_nand_chip *aml_chip);
	int		(*aml_nand_options_confirm)(struct aml_nand_chip *aml_chip);
	void 	(*aml_nand_cmd_ctrl)(struct aml_nand_chip *aml_chip, int cmd,  unsigned int ctrl);
	void	(*aml_nand_select_chip)(struct aml_nand_chip *aml_chip, int chipnr);
	void	(*aml_nand_write_byte)(struct aml_nand_chip *aml_chip, uint8_t data);
	void	(*aml_nand_get_user_byte)(struct aml_nand_chip *aml_chip, unsigned char *oob_buf, int byte_num);
	void	(*aml_nand_set_user_byte)(struct aml_nand_chip *aml_chip, unsigned char *oob_buf, int byte_num);
	void	(*aml_nand_command)(struct aml_nand_chip *aml_chip, unsigned command, int column, int page_addr, int chipnr);
	int		(*aml_nand_wait_devready)(struct aml_nand_chip *aml_chip, int chipnr);
	int		(*aml_nand_dma_read)(struct aml_nand_chip *aml_chip, unsigned char *buf, int len, unsigned bch_mode);
	int		(*aml_nand_dma_write)(struct aml_nand_chip *aml_chip, unsigned char *buf, int len, unsigned bch_mode);
	int		(*aml_nand_hwecc_correct)(struct aml_nand_chip *aml_chip, unsigned char *buf, unsigned size, unsigned char *oob_buf);
};

struct aml_nand_platform {
		struct aml_nand_flash_dev *nand_flash_dev;
		char *name;
		unsigned chip_enable_pad;
		unsigned ready_busy_pad;

		unsigned T_REA;
		unsigned T_RHOH;
		unsigned int		  ran_mode; 			   //def close, for all part
		unsigned int		  rbpin_mode;				   //may get from romboot 
		unsigned int		  rbpin_detect;
		unsigned int		  short_pgsz;			   //zero means no short 
		 		
		 struct aml_nand_chip  *aml_chip;
         struct platform_nand_data platform_nand_data;
};

struct aml_nand_device {
	struct aml_nand_platform *aml_nand_platform;
	u8 dev_num;
	struct notifier_block nb;
	struct pinctrl *nand_pinctrl;
	struct pinctrl_state *nand_rbstate;
	struct pinctrl_state *nand_norbstate;
	struct pinctrl_state *nand_idlestate;
};

#if 0
static pinmux_item_t nand_set_pins[] = {
    {
        .reg = PINMUX_REG(2),
        .setmask = (0xf<<18)|(1 << 17)|(0x3 << 25)| (1<<27),
    },
    {
       .reg = PINMUX_REG(5),
       .clrmask = ((1<<0) | (1<<1) | (1<<2) | (1<<3)),
    },	
    PINMUX_END_ITEM
};

static pinmux_item_t nand_clr_pins[] = {
	{
        .reg = PINMUX_REG(2),
        .setmask = (0x3fF<<18) | (0X3 << 16),
    },
    {
       .reg = PINMUX_REG(5),
       .clrmask = ((1<<0) | (1<<1) | (1<<2) | (1<<3)),
    },	
    PINMUX_END_ITEM
};

static bool nand_chip_select(bool flag)
{
	return flag;
}

static pinmux_set_t nand_set = {
    .chip_select = NULL,
    .pinmux = &nand_set_pins[0]
};

static pinmux_set_t nand_clear = {
    .chip_select = nand_chip_select,
    .pinmux = &nand_clr_pins[0]
};
#endif
extern void aml_pinmux_set(const char *name);
extern void aml_pinmux_clr();
static void inline  nand_get_chip(void )
{
	aml_set_reg32_mask(P_PREG_PAD_GPIO3_EN_N, 0x3ffff);
	aml_set_reg32_mask(P_PAD_PULL_UP_REG3, (0xff | (1<<16)));
	//SET_CBUS_REG_MASK(PERIPHS_PIN_MUX_5, ((1<<7) | (1 << 8) | (1 << 9)));
	//aml_set_reg32_mask(P_PERIPHS_PIN_MUX_2, ((0xf<<18) | (1 << 17) | (0x3 << 25)));
	aml_pinmux_set("nand_base"); 
}
static void inline nand_release_chip(void)
{
	//CLEAR_CBUS_REG_MASK(PERIPHS_PIN_MUX_5, ((1<<7) | (1 << 8) | (1 << 9)));
	//aml_clr_reg32_mask(P_PERIPHS_PIN_MUX_2, ((0x3fF<<18) | (0X3 << 16)));
	aml_pinmux_clr();  
}

static inline struct aml_nand_chip *to_nand_chip(struct platform_device *pdev)
{
	return platform_get_drvdata(pdev);
}

static inline struct aml_nand_chip *mtd_to_nand_chip(struct mtd_info *mtd)
{
	return container_of(mtd, struct aml_nand_chip, mtd);
}

extern int aml_nand_init(struct aml_nand_chip *aml_chip);
extern uint8_t aml_nand_get_onfi_features(struct aml_nand_chip *aml_chip,  uint8_t *buf, int addr);
extern void aml_nand_set_onfi_features(struct aml_nand_chip *aml_chip,  uint8_t *buf, int addr);

#endif // NAND_H_INCLUDED

