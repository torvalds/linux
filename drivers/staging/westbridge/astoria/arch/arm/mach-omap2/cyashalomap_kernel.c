/* Cypress WestBridge OMAP3430 Kernel Hal source file (cyashalomap_kernel.c)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor,
## Boston, MA  02110-1301, USA.
## ===========================
*/

#ifdef CONFIG_MACH_OMAP3_WESTBRIDGE_AST_PNAND_HAL

#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/sched.h>
/* include seems broken moving for patch submission
 * #include <mach/mux.h>
 * #include <mach/gpmc.h>
 * #include <mach/westbridge/westbridge-omap3-pnand-hal/cyashalomap_kernel.h>
 * #include <mach/westbridge/westbridge-omap3-pnand-hal/cyasomapdev_kernel.h>
 * #include <mach/westbridge/westbridge-omap3-pnand-hal/cyasmemmap.h>
 * #include <linux/westbridge/cyaserr.h>
 * #include <linux/westbridge/cyasregs.h>
 * #include <linux/westbridge/cyasdma.h>
 * #include <linux/westbridge/cyasintr.h>
 */
#include <linux/../../arch/arm/plat-omap/include/plat/mux.h>
#include <linux/../../arch/arm/plat-omap/include/plat/gpmc.h>
#include "../plat-omap/include/mach/westbridge/westbridge-omap3-pnand-hal/cyashalomap_kernel.h"
#include "../plat-omap/include/mach/westbridge/westbridge-omap3-pnand-hal/cyasomapdev_kernel.h"
#include "../plat-omap/include/mach/westbridge/westbridge-omap3-pnand-hal/cyasmemmap.h"
#include "../../../include/linux/westbridge/cyaserr.h"
#include "../../../include/linux/westbridge/cyasregs.h"
#include "../../../include/linux/westbridge/cyasdma.h"
#include "../../../include/linux/westbridge/cyasintr.h"

#define HAL_REV "1.1.0"

/*
 * uncomment to enable 16bit pnand interface
 */
#define PNAND_16BIT_MODE

/*
 * selects one of 3 versions of pnand_lbd_read()
 * PNAND_LBD_READ_NO_PFE - original 8/16 bit code
 *    reads through the gpmc CONTROLLER REGISTERS
 * ENABLE_GPMC_PF_ENGINE - USES GPMC PFE FIFO reads, in 8 bit mode,
 *     same speed as the above
 * PFE_LBD_READ_V2 - slightly diffrenet, performance same as above
 */
#define PNAND_LBD_READ_NO_PFE
/* #define ENABLE_GPMC_PF_ENGINE */
/* #define  PFE_LBD_READ_V2 */

/*
 * westbrige astoria ISR options to limit number of
 * back to back DMA transfers per ISR interrupt
 */
#define MAX_DRQ_LOOPS_IN_ISR 4

/*
 * debug prints enabling
 *#define DBGPRN_ENABLED
 *#define DBGPRN_DMA_SETUP_RD
 *#define DBGPRN_DMA_SETUP_WR
 */


/*
 * For performance reasons, we handle storage endpoint transfers upto 4 KB
 * within the HAL itself.
 */
 #define CYASSTORAGE_WRITE_EP_NUM	(4)
 #define CYASSTORAGE_READ_EP_NUM	(8)

/*
 *  size of DMA packet HAL can accept from Storage API
 *  HAL will fragment it into smaller chunks that the P port can accept
 */
#define CYASSTORAGE_MAX_XFER_SIZE	(2*32768)

/*
 *  P port MAX DMA packet size according to interface/ep configurartion
 */
#define HAL_DMA_PKT_SZ 512

#define is_storage_e_p(ep) (((ep) == 2) || ((ep) == 4) || \
				((ep) == 6) || ((ep) == 8))

/*
 * persistant, stores current GPMC interface cfg mode
 */
static uint8_t pnand_16bit;

/*
 * keep processing new WB DRQ in ISR untill all handled (performance feature)
 */
#define PROCESS_MULTIPLE_DRQ_IN_ISR (1)


/*
 * ASTORIA PNAND IF COMMANDS, CASDO - READ, CASDI - WRITE
 */
#define CASDO 0x05
#define CASDI 0x85
#define RDPAGE_B1   0x00
#define RDPAGE_B2   0x30
#define PGMPAGE_B1  0x80
#define PGMPAGE_B2  0x10

/*
 * The type of DMA operation, per endpoint
 */
typedef enum cy_as_hal_dma_type {
	cy_as_hal_read,
	cy_as_hal_write,
	cy_as_hal_none
} cy_as_hal_dma_type;


/*
 * SG list halpers defined in scaterlist.h
#define sg_is_chain(sg)		((sg)->page_link & 0x01)
#define sg_is_last(sg)		((sg)->page_link & 0x02)
#define sg_chain_ptr(sg)	\
	((struct scatterlist *) ((sg)->page_link & ~0x03))
*/
typedef struct cy_as_hal_endpoint_dma {
	cy_bool buffer_valid;
	uint8_t *data_p;
	uint32_t size;
	/*
	 * sg_list_enabled - if true use, r/w DMA transfers use sg list,
	 *		FALSE use pointer to a buffer
	 * sg_p - pointer to the owner's sg list, of there is such
	 *		(like blockdriver)
	 * dma_xfer_sz - size of the next dma xfer on P port
	 * seg_xfer_cnt -  counts xfered bytes for in current sg_list
	 *		memory segment
	 * req_xfer_cnt - total number of bytes transfered so far in
	 *		current request
	 * req_length - total request length
	 */
	bool sg_list_enabled;
	struct scatterlist *sg_p;
	uint16_t dma_xfer_sz;
	uint32_t seg_xfer_cnt;
	uint16_t req_xfer_cnt;
	uint16_t req_length;
	cy_as_hal_dma_type type;
	cy_bool pending;
} cy_as_hal_endpoint_dma;

/*
 * The list of OMAP devices (should be one)
 */
static cy_as_omap_dev_kernel *m_omap_list_p;

/*
 * The callback to call after DMA operations are complete
 */
static cy_as_hal_dma_complete_callback callback;

/*
 * Pending data size for the endpoints
 */
static cy_as_hal_endpoint_dma end_points[16];

/*
 * Forward declaration
 */
static void cy_handle_d_r_q_interrupt(cy_as_omap_dev_kernel *dev_p);

static uint16_t intr_sequence_num;
static uint8_t intr__enable;
spinlock_t int_lock;

static u32 iomux_vma;
static u32 csa_phy;

/*
 * gpmc I/O registers VMA
 */
static u32 gpmc_base;

/*
 * gpmc data VMA associated with CS4 (ASTORIA CS on GPMC)
 */
static u32 gpmc_data_vma;
static u32 ndata_reg_vma;
static u32 ncmd_reg_vma;
static u32 naddr_reg_vma;

/*
 * fwd declarations
 */
static void p_nand_lbd_read(u16 col_addr, u32 row_addr, u16 count, void *buff);
static void p_nand_lbd_write(u16 col_addr, u32 row_addr, u16 count, void *buff);
static inline u16 __attribute__((always_inline))
			ast_p_nand_casdo_read(u8 reg_addr8);
static inline void __attribute__((always_inline))
			ast_p_nand_casdi_write(u8 reg_addr8, u16 data);

/*
 * prints given number of omap registers
 */
static void cy_as_hal_print_omap_regs(char *name_prefix,
				u8 name_base, u32 virt_base, u16 count)
{
	u32 reg_val, reg_addr;
	u16 i;
	cy_as_hal_print_message(KERN_INFO "\n");
	for (i = 0; i < count; i++) {

		reg_addr = virt_base + (i*4);
		/* use virtual addresses here*/
		reg_val = __raw_readl(reg_addr);
		cy_as_hal_print_message(KERN_INFO "%s_%d[%8.8x]=%8.8x\n",
						name_prefix, name_base+i,
						reg_addr, reg_val);
	}
}

/*
 * setMUX function for a pad + additional pad flags
 */
static u16 omap_cfg_reg_L(u32 pad_func_index)
{
	static u8 sanity_check = 1;

	u32 reg_vma;
	u16 cur_val, wr_val, rdback_val;

	/*
	 * do sanity check on the omap_mux_pin_cfg[] table
	 */
	cy_as_hal_print_message(KERN_INFO" OMAP pins user_pad cfg ");
	if (sanity_check) {
		if ((omap_mux_pin_cfg[END_OF_TABLE].name[0] == 'E') &&
			(omap_mux_pin_cfg[END_OF_TABLE].name[1] == 'N') &&
			(omap_mux_pin_cfg[END_OF_TABLE].name[2] == 'D')) {

			cy_as_hal_print_message(KERN_INFO
					"table is good.\n");
		} else {
			cy_as_hal_print_message(KERN_WARNING
					"table is bad, fix it");
		}
		/*
		 * do it only once
		 */
		sanity_check = 0;
	}

	/*
	 * get virtual address to the PADCNF_REG
	 */
	reg_vma = (u32)iomux_vma + omap_mux_pin_cfg[pad_func_index].offset;

	/*
	 * add additional USER PU/PD/EN flags
	 */
	wr_val = omap_mux_pin_cfg[pad_func_index].mux_val;
	cur_val = IORD16(reg_vma);

	/*
	 * PADCFG regs 16 bit long, packed into 32 bit regs,
	 * can also be accessed as u16
	 */
	IOWR16(reg_vma, wr_val);
	rdback_val = IORD16(reg_vma);

	/*
	 * in case if the caller wants to save the old value
	 */
	return wr_val;
}

#define BLKSZ_4K 0x1000

/*
 * switch GPMC DATA bus mode
 */
void cy_as_hal_gpmc_enable_16bit_bus(bool dbus16_enabled)
{
	uint32_t tmp32;

	/*
	 * disable gpmc CS4 operation 1st
	 */
	tmp32 = gpmc_cs_read_reg(AST_GPMC_CS,
				GPMC_CS_CONFIG7) & ~GPMC_CONFIG7_CSVALID;
	gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG7, tmp32);

	/*
	 * GPMC NAND data bus can be 8 or 16 bit wide
	 */
	if (dbus16_enabled) {
		DBGPRN("enabling 16 bit bus\n");
		gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG1,
				(GPMC_CONFIG1_DEVICETYPE(2) |
				GPMC_CONFIG1_WAIT_PIN_SEL(2) |
				GPMC_CONFIG1_DEVICESIZE_16)
				);
	} else {
		DBGPRN(KERN_INFO "enabling 8 bit bus\n");
		gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG1,
				(GPMC_CONFIG1_DEVICETYPE(2) |
				GPMC_CONFIG1_WAIT_PIN_SEL(2))
				);
	}

	/*
	 * re-enable astoria CS operation on GPMC
	 */
	 gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG7,
			(tmp32 | GPMC_CONFIG7_CSVALID));

	/*
	 *remember the state
	 */
	pnand_16bit = dbus16_enabled;
}

static int cy_as_hal_gpmc_init(void)
{
	u32 tmp32;
	int err;
	struct gpmc_timings	timings;
	/*
	 * get GPMC i/o registers base(already been i/o mapped
	 * in kernel, no need for separate i/o remap)
	 */
	gpmc_base = phys_to_virt(OMAP34XX_GPMC_BASE);
	DBGPRN(KERN_INFO "kernel has gpmc_base=%x , val@ the base=%x",
		gpmc_base, __raw_readl(gpmc_base)
	);

	/*
	 * these are globals are full VMAs of the gpmc_base above
	 */
	ncmd_reg_vma = GPMC_VMA(AST_GPMC_NAND_CMD);
	naddr_reg_vma = GPMC_VMA(AST_GPMC_NAND_ADDR);
	ndata_reg_vma = GPMC_VMA(AST_GPMC_NAND_DATA);

	/*
	 * request GPMC CS for ASTORIA request
	 */
	if (gpmc_cs_request(AST_GPMC_CS, SZ_16M, (void *)&csa_phy) < 0) {
		cy_as_hal_print_message(KERN_ERR "error failed to request"
					"ncs4 for ASTORIA\n");
			return -1;
	} else {
		DBGPRN(KERN_INFO "got phy_addr:%x for "
				"GPMC CS%d GPMC_CFGREG7[CS4]\n",
				 csa_phy, AST_GPMC_CS);
	}

	/*
	 * request VM region for 4K addr space for chip select 4 phy address
	 * technically we don't need it for NAND devices, but do it anyway
	 * so that data read/write bus cycle can be triggered by reading
	 * or writing this mem region
	 */
	if (!request_mem_region(csa_phy, BLKSZ_4K, "AST_OMAP_HAL")) {
		err = -EBUSY;
		cy_as_hal_print_message(KERN_ERR "error MEM region "
					"request for phy_addr:%x failed\n",
					csa_phy);
			goto out_free_cs;
	}

	/*
	 * REMAP mem region associated with our CS
	 */
	gpmc_data_vma = (u32)ioremap_nocache(csa_phy, BLKSZ_4K);
	if (!gpmc_data_vma) {
		err = -ENOMEM;
		cy_as_hal_print_message(KERN_ERR "error- ioremap()"
					"for phy_addr:%x failed", csa_phy);

		goto out_release_mem_region;
	}
	cy_as_hal_print_message(KERN_INFO "ioremap(%x) returned vma=%x\n",
							csa_phy, gpmc_data_vma);

	gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG1,
						(GPMC_CONFIG1_DEVICETYPE(2) |
						GPMC_CONFIG1_WAIT_PIN_SEL(2)));

	memset(&timings, 0, sizeof(timings));

	/* cs timing */
	timings.cs_on = WB_GPMC_CS_t_o_n;
	timings.cs_wr_off = WB_GPMC_BUSCYC_t;
	timings.cs_rd_off = WB_GPMC_BUSCYC_t;

	/* adv timing */
	timings.adv_on = WB_GPMC_ADV_t_o_n;
	timings.adv_rd_off = WB_GPMC_BUSCYC_t;
	timings.adv_wr_off = WB_GPMC_BUSCYC_t;

	/* oe timing */
	timings.oe_on = WB_GPMC_OE_t_o_n;
	timings.oe_off = WB_GPMC_OE_t_o_f_f;
	timings.access = WB_GPMC_RD_t_a_c_c;
	timings.rd_cycle = WB_GPMC_BUSCYC_t;

	/* we timing */
	timings.we_on = WB_GPMC_WE_t_o_n;
	timings.we_off = WB_GPMC_WE_t_o_f_f;
	timings.wr_access = WB_GPMC_WR_t_a_c_c;
	timings.wr_cycle = WB_GPMC_BUSCYC_t;

	timings.page_burst_access = WB_GPMC_BUSCYC_t;
	timings.wr_data_mux_bus = WB_GPMC_BUSCYC_t;
	gpmc_cs_set_timings(AST_GPMC_CS, &timings);

	cy_as_hal_print_omap_regs("GPMC_CONFIG", 1,
			GPMC_VMA(GPMC_CFG_REG(1, AST_GPMC_CS)), 7);

	/*
	 * DISABLE cs4, NOTE GPMC REG7 is already configured
	 * at this point by gpmc_cs_request
	 */
	tmp32 = gpmc_cs_read_reg(AST_GPMC_CS, GPMC_CS_CONFIG7) &
						~GPMC_CONFIG7_CSVALID;
	gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG7, tmp32);

	/*
	 * PROGRAM chip select Region, (see OMAP3430 TRM PAGE 1088)
	 */
	gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG7,
					(AS_CS_MASK | AS_CS_BADDR));

	/*
	 * by default configure GPMC into 8 bit mode
	 * (to match astoria default mode)
	 */
	gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG1,
					(GPMC_CONFIG1_DEVICETYPE(2) |
					GPMC_CONFIG1_WAIT_PIN_SEL(2)));

	/*
	 * ENABLE astoria cs operation on GPMC
	 */
	gpmc_cs_write_reg(AST_GPMC_CS, GPMC_CS_CONFIG7,
					(tmp32 | GPMC_CONFIG7_CSVALID));

	/*
	 * No method currently exists to write this register through GPMC APIs
	 * need to change WAIT2 polarity
	 */
	tmp32 = IORD32(GPMC_VMA(GPMC_CONFIG_REG));
	tmp32 = tmp32 | NAND_FORCE_POSTED_WRITE_B | 0x40;
	IOWR32(GPMC_VMA(GPMC_CONFIG_REG), tmp32);

	tmp32 = IORD32(GPMC_VMA(GPMC_CONFIG_REG));
	cy_as_hal_print_message("GPMC_CONFIG_REG=0x%x\n", tmp32);

	return 0;

out_release_mem_region:
	release_mem_region(csa_phy, BLKSZ_4K);

out_free_cs:
	gpmc_cs_free(AST_GPMC_CS);

	return err;
}

/*
 * west bridge astoria ISR (Interrupt handler)
 */
static irqreturn_t cy_astoria_int_handler(int irq,
				void *dev_id, struct pt_regs *regs)
{
	cy_as_omap_dev_kernel *dev_p;
	uint16_t		  read_val = 0;
	uint16_t		  mask_val = 0;

	/*
	* debug stuff, counts number of loops per one intr trigger
	*/
	uint16_t		  drq_loop_cnt = 0;
	uint8_t		   irq_pin;
	/*
	 * flags to watch
	 */
	const uint16_t	sentinel = (CY_AS_MEM_P0_INTR_REG_MCUINT |
				CY_AS_MEM_P0_INTR_REG_MBINT |
				CY_AS_MEM_P0_INTR_REG_PMINT |
				CY_AS_MEM_P0_INTR_REG_PLLLOCKINT);

	/*
	 * sample IRQ pin level (just for statistics)
	 */
	irq_pin = __gpio_get_value(AST_INT);

	/*
	 * this one just for debugging
	 */
	intr_sequence_num++;

	/*
	 * astoria device handle
	 */
	dev_p = dev_id;

	/*
	 * read Astoria intr register
	 */
	read_val = cy_as_hal_read_register((cy_as_hal_device_tag)dev_p,
						CY_AS_MEM_P0_INTR_REG);

	/*
	 * save current mask value
	 */
	mask_val = cy_as_hal_read_register((cy_as_hal_device_tag)dev_p,
						CY_AS_MEM_P0_INT_MASK_REG);

	DBGPRN("<1>HAL__intr__enter:_seq:%d, P0_INTR_REG:%x\n",
			intr_sequence_num, read_val);

	/*
	 * Disable WB interrupt signal generation while we are in ISR
	 */
	cy_as_hal_write_register((cy_as_hal_device_tag)dev_p,
					CY_AS_MEM_P0_INT_MASK_REG, 0x0000);

	/*
	* this is a DRQ Interrupt
	*/
	if (read_val & CY_AS_MEM_P0_INTR_REG_DRQINT) {

		do {
			/*
			 * handle DRQ interrupt
			 */
			drq_loop_cnt++;

			cy_handle_d_r_q_interrupt(dev_p);

			/*
			 * spending to much time in ISR may impact
			 * average system performance
			 */
			if (drq_loop_cnt >= MAX_DRQ_LOOPS_IN_ISR)
				break;

		/*
		 * Keep processing if there is another DRQ int flag
		 */
		} while (cy_as_hal_read_register((cy_as_hal_device_tag)dev_p,
					CY_AS_MEM_P0_INTR_REG) &
					CY_AS_MEM_P0_INTR_REG_DRQINT);
	}

	if (read_val & sentinel)
		cy_as_intr_service_interrupt((cy_as_hal_device_tag)dev_p);

	DBGPRN("<1>_hal:_intr__exit seq:%d, mask=%4.4x,"
			"int_pin:%d DRQ_jobs:%d\n",
			intr_sequence_num,
			mask_val,
			irq_pin,
			drq_loop_cnt);

	/*
	 * re-enable WB hw interrupts
	 */
	cy_as_hal_write_register((cy_as_hal_device_tag)dev_p,
					CY_AS_MEM_P0_INT_MASK_REG, mask_val);

	return IRQ_HANDLED;
}

static int cy_as_hal_configure_interrupts(void *dev_p)
{
	int result;
	int irq_pin  = AST_INT;

	set_irq_type(OMAP_GPIO_IRQ(irq_pin), IRQ_TYPE_LEVEL_LOW);

	/*
	 * for shared IRQS must provide non NULL device ptr
	 * othervise the int won't register
	 * */
	result = request_irq(OMAP_GPIO_IRQ(irq_pin),
					(irq_handler_t)cy_astoria_int_handler,
					IRQF_SHARED, "AST_INT#", dev_p);

	if (result == 0) {
		/*
		 * OMAP_GPIO_IRQ(irq_pin) - omap logical IRQ number
		 *		assigned to this interrupt
		 * OMAP_GPIO_BIT(AST_INT, GPIO_IRQENABLE1) - print status
		 *		of AST_INT GPIO IRQ_ENABLE FLAG
		 */
		cy_as_hal_print_message(KERN_INFO"AST_INT omap_pin:"
				"%d assigned IRQ #%d IRQEN1=%d\n",
				irq_pin,
				OMAP_GPIO_IRQ(irq_pin),
				OMAP_GPIO_BIT(AST_INT, GPIO_IRQENABLE1)
				);
	} else {
		cy_as_hal_print_message("cyasomaphal: interrupt "
				"failed to register\n");
		gpio_free(irq_pin);
		cy_as_hal_print_message(KERN_WARNING
				"ASTORIA: can't get assigned IRQ"
				"%i for INT#\n", OMAP_GPIO_IRQ(irq_pin));
	}

	return result;
}

/*
 * initialize OMAP pads/pins to user defined functions
 */
static void cy_as_hal_init_user_pads(user_pad_cfg_t *pad_cfg_tab)
{
	/*
	 * browse through the table an dinitiaze the pins
	 */
	u32 in_level = 0;
	u16 tmp16, mux_val;

	while (pad_cfg_tab->name != NULL) {

		if (gpio_request(pad_cfg_tab->pin_num, NULL) == 0) {

			pad_cfg_tab->valid = 1;
			mux_val = omap_cfg_reg_L(pad_cfg_tab->mux_func);

			/*
			 * always set drv level before changing out direction
			 */
			__gpio_set_value(pad_cfg_tab->pin_num,
							pad_cfg_tab->drv);

			/*
			 * "0" - OUT, "1", input omap_set_gpio_direction
			 * (pad_cfg_tab->pin_num, pad_cfg_tab->dir);
			 */
			if (pad_cfg_tab->dir)
				gpio_direction_input(pad_cfg_tab->pin_num);
			else
				gpio_direction_output(pad_cfg_tab->pin_num,
							pad_cfg_tab->drv);

			/*  sample the pin  */
			in_level = __gpio_get_value(pad_cfg_tab->pin_num);

			cy_as_hal_print_message(KERN_INFO "configured %s to "
					"OMAP pad_%d, DIR=%d "
					"DOUT=%d, DIN=%d\n",
					pad_cfg_tab->name,
					pad_cfg_tab->pin_num,
					pad_cfg_tab->dir,
					pad_cfg_tab->drv,
					in_level
			);
		} else {
			/*
			 * get the pad_mux value to check on the pin_function
			 */
			cy_as_hal_print_message(KERN_INFO "couldn't cfg pin %d"
					"for signal %s, its already taken\n",
					pad_cfg_tab->pin_num,
					pad_cfg_tab->name);
		}

		tmp16 = *(u16 *)PADCFG_VMA
			(omap_mux_pin_cfg[pad_cfg_tab->mux_func].offset);

		cy_as_hal_print_message(KERN_INFO "GPIO_%d(PAD_CFG=%x,OE=%d"
			"DOUT=%d, DIN=%d IRQEN=%d)\n\n",
			pad_cfg_tab->pin_num, tmp16,
			OMAP_GPIO_BIT(pad_cfg_tab->pin_num, GPIO_OE),
			OMAP_GPIO_BIT(pad_cfg_tab->pin_num, GPIO_DATA_OUT),
			OMAP_GPIO_BIT(pad_cfg_tab->pin_num, GPIO_DATA_IN),
			OMAP_GPIO_BIT(pad_cfg_tab->pin_num, GPIO_IRQENABLE1)
			);

		/*
		 * next pad_cfg deriptor
		 */
		pad_cfg_tab++;
	}

	cy_as_hal_print_message(KERN_INFO"pads configured\n");
}


/*
 * release gpios taken by the module
 */
static void cy_as_hal_release_user_pads(user_pad_cfg_t *pad_cfg_tab)
{
	while (pad_cfg_tab->name != NULL) {

		if (pad_cfg_tab->valid) {
			gpio_free(pad_cfg_tab->pin_num);
			pad_cfg_tab->valid = 0;
			cy_as_hal_print_message(KERN_INFO "GPIO_%d "
					"released from %s\n",
					pad_cfg_tab->pin_num,
					pad_cfg_tab->name);
		} else {
			cy_as_hal_print_message(KERN_INFO "no release "
					"for %s, GPIO_%d, wasn't acquired\n",
					pad_cfg_tab->name,
					pad_cfg_tab->pin_num);
		}
		pad_cfg_tab++;
	}
}

void cy_as_hal_config_c_s_mux(void)
{
	/*
	 * FORCE the GPMC CS4 pin (it is in use by the  zoom system)
	 */
	omap_cfg_reg_L(T8_OMAP3430_GPMC_n_c_s4);
}
EXPORT_SYMBOL(cy_as_hal_config_c_s_mux);

/*
 * inits all omap h/w
 */
uint32_t cy_as_hal_processor_hw_init(void)
{
	int i, err;

	cy_as_hal_print_message(KERN_INFO "init OMAP3430 hw...\n");

	iomux_vma = (u32)ioremap_nocache(
				(u32)CTLPADCONF_BASE_ADDR, CTLPADCONF_SIZE);
	cy_as_hal_print_message(KERN_INFO "PADCONF_VMA=%x val=%x\n",
				iomux_vma, IORD32(iomux_vma));

	/*
	 * remap gpio banks
	 */
	for (i = 0; i < 6; i++) {
		gpio_vma_tab[i].virt_addr = (u32)ioremap_nocache(
					gpio_vma_tab[i].phy_addr,
					gpio_vma_tab[i].size);

		cy_as_hal_print_message(KERN_INFO "%s virt_addr=%x\n",
					gpio_vma_tab[i].name,
					(u32)gpio_vma_tab[i].virt_addr);
	};

	/*
	 * force OMAP_GPIO_126  to rleased state,
	 * will be configured to drive reset
	 */
	gpio_free(AST_RESET);

	/*
	 *same thing with AStoria CS pin
	 */
	gpio_free(AST_CS);

	/*
	 * initialize all the OMAP pads connected to astoria
	 */
	cy_as_hal_init_user_pads(user_pad_cfg);

	err = cy_as_hal_gpmc_init();
	if (err < 0)
		cy_as_hal_print_message(KERN_INFO"gpmc init failed:%d", err);

	cy_as_hal_config_c_s_mux();

	return gpmc_data_vma;
}
EXPORT_SYMBOL(cy_as_hal_processor_hw_init);

void cy_as_hal_omap_hardware_deinit(cy_as_omap_dev_kernel *dev_p)
{
	/*
	 * free omap hw resources
	 */
	if (gpmc_data_vma != 0)
		iounmap((void *)gpmc_data_vma);

	if (csa_phy != 0)
		release_mem_region(csa_phy, BLKSZ_4K);

	gpmc_cs_free(AST_GPMC_CS);

	free_irq(OMAP_GPIO_IRQ(AST_INT), dev_p);

	cy_as_hal_release_user_pads(user_pad_cfg);
}

/*
 * These are the functions that are not part of the
 * HAL layer, but are required to be called for this HAL
 */

/*
 * Called On AstDevice LKM exit
 */
int stop_o_m_a_p_kernel(const char *pgm, cy_as_hal_device_tag tag)
{
	cy_as_omap_dev_kernel *dev_p = (cy_as_omap_dev_kernel *)tag;

	/*
	 * TODO: Need to disable WB interrupt handlere 1st
	 */
	if (0 == dev_p)
		return 1;

	cy_as_hal_print_message("<1>_stopping OMAP34xx HAL layer object\n");
	if (dev_p->m_sig != CY_AS_OMAP_KERNEL_HAL_SIG) {
		cy_as_hal_print_message("<1>%s: %s: bad HAL tag\n",
								pgm, __func__);
		return 1;
	}

	/*
	 * disable interrupt
	 */
	cy_as_hal_write_register((cy_as_hal_device_tag)dev_p,
			CY_AS_MEM_P0_INT_MASK_REG, 0x0000);

#if 0
	if (dev_p->thread_flag == 0) {
		dev_p->thread_flag = 1;
		wait_for_completion(&dev_p->thread_complete);
		cy_as_hal_print_message("cyasomaphal:"
			"done cleaning thread\n");
		cy_as_hal_destroy_sleep_channel(&dev_p->thread_sc);
	}
#endif

	cy_as_hal_omap_hardware_deinit(dev_p);

	/*
	 * Rearrange the list
	 */
	if (m_omap_list_p == dev_p)
		m_omap_list_p = dev_p->m_next_p;

	cy_as_hal_free(dev_p);

	cy_as_hal_print_message(KERN_INFO"OMAP_kernel_hal stopped\n");
	return 0;
}

int omap_start_intr(cy_as_hal_device_tag tag)
{
	cy_as_omap_dev_kernel *dev_p = (cy_as_omap_dev_kernel *)tag;
	int ret = 0;
	const uint16_t mask = CY_AS_MEM_P0_INTR_REG_DRQINT |
				CY_AS_MEM_P0_INTR_REG_MBINT;

	/*
	 * register for interrupts
	 */
	ret = cy_as_hal_configure_interrupts(dev_p);

	/*
	 * enable only MBox & DRQ interrupts for now
	 */
	cy_as_hal_write_register((cy_as_hal_device_tag)dev_p,
				CY_AS_MEM_P0_INT_MASK_REG, mask);

	return 1;
}

/*
 * Below are the functions that communicate with the WestBridge device.
 * These are system dependent and must be defined by the HAL layer
 * for a given system.
 */

/*
 * GPMC NAND command+addr write phase
 */
static inline void nand_cmd_n_addr(u8 cmdb1, u16 col_addr, u32 row_addr)
{
	/*
	 * byte order on the bus <cmd> <CA0,CA1,RA0,RA1, RA2>
	 */
	u32 tmpa32 = ((row_addr << 16) | col_addr);
	u8 RA2 = (u8)(row_addr >> 16);

	if (!pnand_16bit) {
		/*
		 * GPMC PNAND 8bit BUS
		 */
		/*
		 * CMD1
		 */
		IOWR8(ncmd_reg_vma, cmdb1);

		/*
		 *pnand bus: <CA0,CA1,RA0,RA1>
		 */
		IOWR32(naddr_reg_vma, tmpa32);

		/*
		 * <RA2> , always zero
		 */
		IOWR8(naddr_reg_vma, RA2);

	} else {
		/*
		 * GPMC PNAND 16bit BUS , in 16 bit mode CMD
		 * and ADDR sent on [d7..d0]
		 */
		uint8_t CA0, CA1, RA0, RA1;
		CA0 = tmpa32 & 0x000000ff;
		CA1 = (tmpa32 >> 8) &  0x000000ff;
		RA0 = (tmpa32 >> 16) & 0x000000ff;
		RA1 = (tmpa32 >> 24) & 0x000000ff;

		/*
		 * can't use 32 bit writes here omap will not serialize
		 * them to lower half in16 bit mode
		 */

		/*
		 *pnand bus: <CMD1, CA0,CA1,RA0,RA1, RA2 (always zero)>
		 */
		IOWR8(ncmd_reg_vma, cmdb1);
		IOWR8(naddr_reg_vma, CA0);
		IOWR8(naddr_reg_vma, CA1);
		IOWR8(naddr_reg_vma, RA0);
		IOWR8(naddr_reg_vma, RA1);
		IOWR8(naddr_reg_vma, RA2);
	}
}

/*
 * spin until r/b goes high
 */
inline int wait_rn_b_high(void)
{
	u32 w_spins = 0;

	/*
	 * TODO: note R/b may go low here, need to spin until high
	 * while (omap_get_gpio_datain(AST_RnB) == 0) {
	 * w_spins++;
	 * }
	 * if (OMAP_GPIO_BIT(AST_RnB, GPIO_DATA_IN)  == 0) {
	 *
	 * while (OMAP_GPIO_BIT(AST_RnB, GPIO_DATA_IN)  == 0) {
	 * w_spins++;
	 * }
	 * printk("<1>RnB=0!:%d\n",w_spins);
	 * }
	 */
	return w_spins;
}

#ifdef ENABLE_GPMC_PF_ENGINE
/* #define PFE_READ_DEBUG
 * PNAND  block read with OMAP PFE enabled
 * status: Not tested, NW, broken , etc
 */
static void p_nand_lbd_read(u16 col_addr, u32 row_addr, u16 count, void *buff)
{
	uint16_t w32cnt;
	uint32_t *ptr32;
	uint8_t *ptr8;
	uint8_t  bytes_in_fifo;

	/* debug vars*/
#ifdef PFE_READ_DEBUG
	uint32_t loop_limit;
	uint16_t bytes_read = 0;
#endif

	/*
	 * configure the prefetch engine
	 */
	uint32_t tmp32;
	uint32_t pfe_status;

	/*
	 * DISABLE GPMC CS4 operation 1st, this is
	 * in case engine is be already disabled
	 */
	IOWR32(GPMC_VMA(GPMC_PREFETCH_CONTROL), 0x0);
	IOWR32(GPMC_VMA(GPMC_PREFETCH_CONFIG1), GPMC_PREFETCH_CONFIG1_VAL);
	IOWR32(GPMC_VMA(GPMC_PREFETCH_CONFIG2), count);

#ifdef PFE_READ_DEBUG
	tmp32 = IORD32(GPMC_VMA(GPMC_PREFETCH_CONFIG1));
	if (tmp32 != GPMC_PREFETCH_CONFIG1_VAL) {
		printk(KERN_INFO "<1> prefetch is CONFIG1 read val:%8.8x, != VAL written:%8.8x\n",
				tmp32, GPMC_PREFETCH_CONFIG1_VAL);
		tmp32 = IORD32(GPMC_VMA(GPMC_PREFETCH_STATUS));
		printk(KERN_INFO "<1> GPMC_PREFETCH_STATUS : %8.8x\n", tmp32);
	}

	/*
	 *sanity check 2
	 */
	tmp32 = IORD32(GPMC_VMA(GPMC_PREFETCH_CONFIG2));
	if (tmp32 != (count))
		printk(KERN_INFO "<1> GPMC_PREFETCH_CONFIG2 read val:%d, "
				"!= VAL written:%d\n", tmp32, count);
#endif

	/*
	 * ISSUE PNAND CMD+ADDR, note gpmc puts 32b words
	 * on the bus least sig. byte 1st
	 */
	nand_cmd_n_addr(RDPAGE_B1, col_addr, row_addr);

	IOWR8(ncmd_reg_vma, RDPAGE_B2);

	/*
	 * start the prefetch engine
	 */
	IOWR32(GPMC_VMA(GPMC_PREFETCH_CONTROL), 0x1);

	ptr32 = buff;

	while (1) {
		/*
		 * GPMC PFE service loop
		 */
		do {
			/*
			 * spin until PFE fetched some
			 * PNAND bus words in the FIFO
			 */
			pfe_status = IORD32(GPMC_VMA(GPMC_PREFETCH_STATUS));
			bytes_in_fifo = (pfe_status >> 24) & 0x7f;
		} while (bytes_in_fifo == 0);

		/* whole 32 bit words in fifo */
		w32cnt = bytes_in_fifo >> 2;

#if 0
	   /*
		*NOTE: FIFO_PTR indicates number of NAND bus words bytes
		*   already received in the FIFO and available to be read
		*   by DMA or MPU whether COUNTVAL indicates number of BUS
		*   words yet to be read from PNAND bus words
		*/
		printk(KERN_ERR "<1> got PF_STATUS:%8.8x FIFO_PTR:%d, COUNTVAL:%d, w32cnt:%d\n",
					pfe_status, bytes_in_fifo,
					(pfe_status & 0x3fff), w32cnt);
#endif

		while (w32cnt--)
			*ptr32++ = IORD32(gpmc_data_vma);

		if ((pfe_status & 0x3fff) == 0) {
			/*
			 * PFE acc angine done, there still may be data leftover
			 * in the FIFO re-read FIFO BYTE counter (check for
			 * leftovers from 32 bit read accesses above)
			 */
			bytes_in_fifo = (IORD32(
				GPMC_VMA(GPMC_PREFETCH_STATUS)) >> 24) & 0x7f;

			/*
			 * NOTE we may still have one word left in the fifo
			 * read it out
			 */
			ptr8 = ptr32;
			switch (bytes_in_fifo) {

			case 0:
				/*
				 * nothing to do we already read the
				 * FIFO out with 32 bit accesses
				 */
				break;
			case 1:
				/*
				* this only possible
				* for 8 bit pNAND only
				*/
				*ptr8 = IORD8(gpmc_data_vma);
				break;

			case 2:
				/*
				 * this one can occur in either modes
				 */
				*(uint16_t *)ptr8 = IORD16(gpmc_data_vma);
				break;

			case 3:
				/*
				 * this only possible for 8 bit pNAND only
				 */
				*(uint16_t *)ptr8 = IORD16(gpmc_data_vma);
				ptr8 += 2;
				*ptr8 = IORD8(gpmc_data_vma);
				break;

			case 4:
				/*
				 * shouldn't happen, but has been seen
				 * in 8 bit mode
				 */
				*ptr32 = IORD32(gpmc_data_vma);
				break;

			default:
				printk(KERN_ERR"<1>_error: PFE FIFO bytes leftover is not read:%d\n",
								bytes_in_fifo);
				break;
			}
			/*
			 * read is completed, get out of the while(1) loop
			 */
			break;
		}
	}
}
#endif

#ifdef PFE_LBD_READ_V2
/*
 * PFE engine assisted reads with the 64 byte blocks
 */
static void p_nand_lbd_read(u16 col_addr, u32 row_addr, u16 count, void *buff)
{
	uint8_t rd_cnt;
	uint32_t *ptr32;
	uint8_t  *ptr8;
	uint16_t reminder;
	uint32_t pfe_status;

	/*
	 * ISSUE PNAND CMD+ADDR
	 * note gpmc puts 32b words on the bus least sig. byte 1st
	 */
	nand_cmd_n_addr(RDPAGE_B1, col_addr, row_addr);
	IOWR8(ncmd_reg_vma, RDPAGE_B2);

	/*
	 * setup PFE block
	 * count - OMAP number of bytes to access on pnand bus
	 */

	IOWR32(GPMC_VMA(GPMC_PREFETCH_CONFIG1), GPMC_PREFETCH_CONFIG1_VAL);
	IOWR32(GPMC_VMA(GPMC_PREFETCH_CONFIG2), count);
	IOWR32(GPMC_VMA(GPMC_PREFETCH_CONTROL), 0x1);

	ptr32 = buff;

	do {
		pfe_status = IORD32(GPMC_VMA(GPMC_PREFETCH_STATUS));
		rd_cnt =  pfe_status >> (24+2);

		while (rd_cnt--)
			*ptr32++ = IORD32(gpmc_data_vma);

	} while (pfe_status & 0x3fff);

	/*
	 * read out the leftover
	 */
	ptr8 = ptr32;
	rd_cnt = (IORD32(GPMC_VMA(GPMC_PREFETCH_STATUS))  >> 24) & 0x7f;

	while (rd_cnt--)
		*ptr8++ = IORD8(gpmc_data_vma);
}
#endif

#ifdef PNAND_LBD_READ_NO_PFE
/*
 * Endpoint buffer read  w/o OMAP GPMC Prefetch Engine
 * the original working code, works at max speed for 8 bit xfers
 * for 16 bit the bus diagram has gaps
 */
static void p_nand_lbd_read(u16 col_addr, u32 row_addr, u16 count, void *buff)
{
	uint16_t w32cnt;
	uint32_t *ptr32;
	uint16_t *ptr16;
	uint16_t remainder;

	DBGPRN("<1> %s(): NO_PFE\n", __func__);

	ptr32 = buff;
	/* number of whole 32 bit words in the transfer */
	w32cnt = count >> 2;

	/* remainder, in bytes(0..3) */
	remainder =  count & 03;

	/*
	 * note gpmc puts 32b words on the bus least sig. byte 1st
	 */
	nand_cmd_n_addr(RDPAGE_B1, col_addr, row_addr);
	IOWR8(ncmd_reg_vma, RDPAGE_B2);

	/*
	 * read data by 32 bit chunks
	 */
	while (w32cnt--)
		*ptr32++ = IORD32(ndata_reg_vma);

	/*
	 * now do the remainder(it can be 0, 1, 2 or 3)
	 * same code for both 8 & 16 bit bus
	 * do 1 or 2 MORE words
	 */
	ptr16 = (uint16_t *)ptr32;

	switch (remainder) {
	case 1:
		/*  read one 16 bit word
		 * IN 8 BIT WE NEED TO READ even number of bytes
		 */
	case 2:
		*ptr16 = IORD16(ndata_reg_vma);
		break;
	case 3:
		/*
		 * for 3 bytes read 2 16 bit words
		 */
		*ptr16++ = IORD16(ndata_reg_vma);
		*ptr16   = IORD16(ndata_reg_vma);
		break;
	default:
		/*
		 * remainder is 0
		 */
		break;
	}
}
#endif

/*
 * uses LBD mode to write N bytes into astoria
 * Status: Working, however there are 150ns idle
 * timeafter every 2 (16 bit or 4(8 bit) bus cycles
 */
static void p_nand_lbd_write(u16 col_addr, u32 row_addr, u16 count, void *buff)
{
	uint16_t w32cnt;
	uint16_t remainder;
	uint8_t  *ptr8;
	uint16_t *ptr16;
	uint32_t *ptr32;

	remainder =  count & 03;
	w32cnt = count >> 2;
	ptr32 = buff;
	ptr8 = buff;

	/*
	 * send: CMDB1, CA0,CA1,RA0,RA1,RA2
	 */
	nand_cmd_n_addr(PGMPAGE_B1, col_addr, row_addr);

	/*
	 * blast the data out in 32bit chunks
	 */
	while (w32cnt--)
		IOWR32(ndata_reg_vma, *ptr32++);

	/*
	 * do the reminder if there is one
	 * same handling for both 8 & 16 bit pnand: mode
	 */
	ptr16 = (uint16_t *)ptr32; /* do 1 or 2  words */

	switch (remainder) {
	case 1:
		/*
		 * read one 16 bit word
		 */
	case 2:
		IOWR16(ndata_reg_vma, *ptr16);
		break;

	case 3:
		/*
		 * for 3 bytes read 2 16 bit words
		 */
		IOWR16(ndata_reg_vma, *ptr16++);
		IOWR16(ndata_reg_vma, *ptr16);
		break;
	default:
		/*
		 * reminder is 0
		 */
		break;
	}
	/*
	 * finally issue a PGM cmd
	 */
	IOWR8(ncmd_reg_vma, PGMPAGE_B2);
}

/*
 * write Astoria register
 */
static inline void ast_p_nand_casdi_write(u8 reg_addr8, u16 data)
{
	unsigned long flags;
	u16 addr16;
	/*
	 * throw an error if called from multiple threads
	 */
	static atomic_t rdreg_usage_cnt = { 0 };

	/*
	 * disable interrupts
	 */
	local_irq_save(flags);

	if (atomic_read(&rdreg_usage_cnt) != 0) {
		cy_as_hal_print_message(KERN_ERR "cy_as_omap_hal:"
				"* cy_as_hal_write_register usage:%d\n",
				atomic_read(&rdreg_usage_cnt));
	}

	atomic_inc(&rdreg_usage_cnt);

	/*
	 * 2 flavors of GPMC -> PNAND  access
	 */
	if (pnand_16bit) {
		/*
		 *  16 BIT gpmc NAND mode
		 */

		/*
		 * CMD1, CA1, CA2,
		 */
		IOWR8(ncmd_reg_vma, 0x85);
		IOWR8(naddr_reg_vma, reg_addr8);
		IOWR8(naddr_reg_vma, 0x0c);

		/*
		 * this should be sent on the 16 bit bus
		 */
		IOWR16(ndata_reg_vma, data);
	} else {
		/*
		 * 8 bit nand mode GPMC will automatically
		 * seriallize 16bit or 32 bit writes into
		 * 8 bit onesto the lower 8 bit in LE order
		 */
		addr16 = 0x0c00 | reg_addr8;

		/*
		 * CMD1, CA1, CA2,
		 */
		IOWR8(ncmd_reg_vma, 0x85);
		IOWR16(naddr_reg_vma, addr16);
		IOWR16(ndata_reg_vma, data);
	}

	/*
	 * re-enable interrupts
	 */
	atomic_dec(&rdreg_usage_cnt);
	local_irq_restore(flags);
}


/*
 * read astoria register via pNAND interface
 */
static inline u16 ast_p_nand_casdo_read(u8 reg_addr8)
{
	u16 data;
	u16 addr16;
	unsigned long flags;
	/*
	 * throw an error if called from multiple threads
	 */
	static atomic_t wrreg_usage_cnt = { 0 };

	/*
	 * disable interrupts
	 */
	local_irq_save(flags);

	if (atomic_read(&wrreg_usage_cnt) != 0) {
		/*
		 * if it gets here ( from other threads), this function needs
		 * need spin_lock_irq save() protection
		 */
		cy_as_hal_print_message(KERN_ERR"cy_as_omap_hal: "
				"cy_as_hal_write_register usage:%d\n",
				atomic_read(&wrreg_usage_cnt));
	}
	atomic_inc(&wrreg_usage_cnt);

	/*
	 * 2 flavors of GPMC -> PNAND  access
	 */
	if (pnand_16bit) {
		/*
		 *  16 BIT gpmc NAND mode
		 *  CMD1, CA1, CA2,
		 */

		IOWR8(ncmd_reg_vma, 0x05);
		IOWR8(naddr_reg_vma, reg_addr8);
		IOWR8(naddr_reg_vma, 0x0c);
		IOWR8(ncmd_reg_vma, 0x00E0);

		udelay(1);

		/*
		 * much faster through the gPMC Register space
		 */
		data = IORD16(ndata_reg_vma);
	} else {
		/*
		 *  8 BIT gpmc NAND mode
		 *  CMD1, CA1, CA2, CMD2
		 */
		addr16 = 0x0c00 | reg_addr8;
		IOWR8(ncmd_reg_vma, 0x05);
		IOWR16(naddr_reg_vma, addr16);
		IOWR8(ncmd_reg_vma, 0xE0);
		udelay(1);
		data = IORD16(ndata_reg_vma);
	}

	/*
	 * re-enable interrupts
	 */
	atomic_dec(&wrreg_usage_cnt);
	local_irq_restore(flags);

	return data;
}


/*
 * This function must be defined to write a register within the WestBridge
 * device.  The addr value is the address of the register to write with
 * respect to the base address of the WestBridge device.
 */
void cy_as_hal_write_register(
					cy_as_hal_device_tag tag,
					uint16_t addr, uint16_t data)
{
	ast_p_nand_casdi_write((u8)addr, data);
}

/*
 * This function must be defined to read a register from the WestBridge
 * device.  The addr value is the address of the register to read with
 * respect to the base address of the WestBridge device.
 */
uint16_t cy_as_hal_read_register(cy_as_hal_device_tag tag, uint16_t addr)
{
	uint16_t data  = 0;

	/*
	 * READ ASTORIA REGISTER USING CASDO
	 */
	data = ast_p_nand_casdo_read((u8)addr);

	return data;
}

/*
 * preps Ep pointers & data counters for next packet
 * (fragment of the request) xfer returns true if
 * there is a next transfer, and false if all bytes in
 * current request have been xfered
 */
static inline bool prep_for_next_xfer(cy_as_hal_device_tag tag, uint8_t ep)
{

	if (!end_points[ep].sg_list_enabled) {
		/*
		 * no further transfers for non storage EPs
		 * (like EP2 during firmware download, done
		 * in 64 byte chunks)
		 */
		if (end_points[ep].req_xfer_cnt >= end_points[ep].req_length) {
			DBGPRN("<1> %s():RQ sz:%d non-_sg EP:%d completed\n",
				__func__, end_points[ep].req_length, ep);

			/*
			 * no more transfers, we are done with the request
			 */
			return false;
		}

		/*
		 * calculate size of the next DMA xfer, corner
		 * case for non-storage EPs where transfer size
		 * is not egual N * HAL_DMA_PKT_SZ xfers
		 */
		if ((end_points[ep].req_length - end_points[ep].req_xfer_cnt)
		>= HAL_DMA_PKT_SZ) {
				end_points[ep].dma_xfer_sz = HAL_DMA_PKT_SZ;
		} else {
			/*
			 * that would be the last chunk less
			 * than P-port max size
			 */
			end_points[ep].dma_xfer_sz = end_points[ep].req_length -
					end_points[ep].req_xfer_cnt;
		}

		return true;
	}

	/*
	 * for SG_list assisted dma xfers
	 * are we done with current SG ?
	 */
	if (end_points[ep].seg_xfer_cnt ==  end_points[ep].sg_p->length) {
		/*
		 *  was it the Last SG segment on the list ?
		 */
		if (sg_is_last(end_points[ep].sg_p)) {
			DBGPRN("<1> %s: EP:%d completed,"
					"%d bytes xfered\n",
					__func__,
					ep,
					end_points[ep].req_xfer_cnt
			);

			return false;
		} else {
			/*
			 * There are more SG segments in current
			 * request's sg list setup new segment
			 */

			end_points[ep].seg_xfer_cnt = 0;
			end_points[ep].sg_p = sg_next(end_points[ep].sg_p);
			/* set data pointer for next DMA sg transfer*/
			end_points[ep].data_p = sg_virt(end_points[ep].sg_p);
			DBGPRN("<1> %s new SG:_va:%p\n\n",
					__func__, end_points[ep].data_p);
		}

	}

	/*
	 * for sg list xfers it will always be 512 or 1024
	 */
	end_points[ep].dma_xfer_sz = HAL_DMA_PKT_SZ;

	/*
	 * next transfer is required
	 */

	return true;
}

/*
 * Astoria DMA read request, APP_CPU reads from WB ep buffer
 */
static void cy_service_e_p_dma_read_request(
			cy_as_omap_dev_kernel *dev_p, uint8_t ep)
{
	cy_as_hal_device_tag tag = (cy_as_hal_device_tag)dev_p;
	uint16_t  v, size;
	void	*dptr;
	uint16_t col_addr = 0x0000;
	uint32_t row_addr = CYAS_DEV_CALC_EP_ADDR(ep);
	uint16_t ep_dma_reg = CY_AS_MEM_P0_EP2_DMA_REG + ep - 2;

	/*
	 * get the XFER size frtom WB eP DMA REGISTER
	 */
	v = cy_as_hal_read_register(tag, ep_dma_reg);

	/*
	 * amount of data in EP buff in  bytes
	 */
	size =  v & CY_AS_MEM_P0_E_pn_DMA_REG_COUNT_MASK;

	/*
	 * memory pointer for this DMA packet xfer (sub_segment)
	 */
	dptr = end_points[ep].data_p;

	DBGPRN("<1>HAL:_svc_dma_read on EP_%d sz:%d, intr_seq:%d, dptr:%p\n",
		ep,
		size,
		intr_sequence_num,
		dptr
	);

	cy_as_hal_assert(size != 0);

	if (size) {
		/*
		 * the actual WB-->OMAP memory "soft" DMA xfer
		 */
		p_nand_lbd_read(col_addr, row_addr, size, dptr);
	}

	/*
	 * clear DMAVALID bit indicating that the data has been read
	 */
	cy_as_hal_write_register(tag, ep_dma_reg, 0);

	end_points[ep].seg_xfer_cnt += size;
	end_points[ep].req_xfer_cnt += size;

	/*
	 *  pre-advance data pointer (if it's outside sg
	 * list it will be reset anyway
	 */
	end_points[ep].data_p += size;

	if (prep_for_next_xfer(tag, ep)) {
		/*
		 * we have more data to read in this request,
		 * setup next dma packet due tell WB how much
		 * data we are going to xfer next
		 */
		v = end_points[ep].dma_xfer_sz/*HAL_DMA_PKT_SZ*/ |
				CY_AS_MEM_P0_E_pn_DMA_REG_DMAVAL;
		cy_as_hal_write_register(tag, ep_dma_reg, v);
	} else {
		end_points[ep].pending	  = cy_false;
		end_points[ep].type		 = cy_as_hal_none;
		end_points[ep].buffer_valid = cy_false;

		/*
		 * notify the API that we are done with rq on this EP
		 */
		if (callback) {
			DBGPRN("<1>trigg rd_dma completion cb: xfer_sz:%d\n",
				end_points[ep].req_xfer_cnt);
				callback(tag, ep,
					end_points[ep].req_xfer_cnt,
					CY_AS_ERROR_SUCCESS);
		}
	}
}

/*
 * omap_cpu needs to transfer data to ASTORIA EP buffer
 */
static void cy_service_e_p_dma_write_request(
			cy_as_omap_dev_kernel *dev_p, uint8_t ep)
{
	uint16_t  addr;
	uint16_t v  = 0;
	uint32_t  size;
	uint16_t col_addr = 0x0000;
	uint32_t row_addr = CYAS_DEV_CALC_EP_ADDR(ep);
	void	*dptr;

	cy_as_hal_device_tag tag = (cy_as_hal_device_tag)dev_p;
	/*
	 * note: size here its the size of the dma transfer could be
	 * anything > 0 && < P_PORT packet size
	 */
	size = end_points[ep].dma_xfer_sz;
	dptr = end_points[ep].data_p;

	/*
	 * perform the soft DMA transfer, soft in this case
	 */
	if (size)
		p_nand_lbd_write(col_addr, row_addr, size, dptr);

	end_points[ep].seg_xfer_cnt += size;
	end_points[ep].req_xfer_cnt += size;
	/*
	 * pre-advance data pointer
	 * (if it's outside sg list it will be reset anyway)
	 */
	end_points[ep].data_p += size;

	/*
	 * now clear DMAVAL bit to indicate we are done
	 * transferring data and that the data can now be
	 * sent via USB to the USB host, sent to storage,
	 * or used internally.
	 */

	addr = CY_AS_MEM_P0_EP2_DMA_REG + ep - 2;
	cy_as_hal_write_register(tag, addr, size);

	/*
	 * finally, tell the USB subsystem that the
	 * data is gone and we can accept the
	 * next request if one exists.
	 */
	if (prep_for_next_xfer(tag, ep)) {
		/*
		 * There is more data to go. Re-init the WestBridge DMA side
		 */
		v = end_points[ep].dma_xfer_sz |
			CY_AS_MEM_P0_E_pn_DMA_REG_DMAVAL;
		cy_as_hal_write_register(tag, addr, v);
	} else {

	   end_points[ep].pending	  = cy_false;
	   end_points[ep].type		 = cy_as_hal_none;
	   end_points[ep].buffer_valid = cy_false;

		/*
		 * notify the API that we are done with rq on this EP
		 */
		if (callback) {
			/*
			 * this callback will wake up the process that might be
			 * sleeping on the EP which data is being transferred
			 */
			callback(tag, ep,
					end_points[ep].req_xfer_cnt,
					CY_AS_ERROR_SUCCESS);
		}
	}
}

/*
 * HANDLE DRQINT from Astoria (called in AS_Intr context
 */
static void cy_handle_d_r_q_interrupt(cy_as_omap_dev_kernel *dev_p)
{
	uint16_t v;
	static uint8_t service_ep = 2;

	/*
	 * We've got DRQ INT, read DRQ STATUS Register */
	v = cy_as_hal_read_register((cy_as_hal_device_tag)dev_p,
			CY_AS_MEM_P0_DRQ);

	if (v == 0) {
#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("stray DRQ interrupt detected\n");
#endif
		return;
	}

	/*
	 * Now, pick a given DMA request to handle, for now, we just
	 * go round robin.  Each bit position in the service_mask
	 * represents an endpoint from EP2 to EP15.  We rotate through
	 * each of the endpoints to find one that needs to be serviced.
	 */
	while ((v & (1 << service_ep)) == 0) {

		if (service_ep == 15)
			service_ep = 2;
		else
			service_ep++;
	}

	if (end_points[service_ep].type == cy_as_hal_write) {
		/*
		 * handle DMA WRITE REQUEST: app_cpu will
		 * write data into astoria EP buffer
		 */
		cy_service_e_p_dma_write_request(dev_p, service_ep);
	} else if (end_points[service_ep].type == cy_as_hal_read) {
		/*
		 * handle DMA READ REQUEST: cpu will
		 * read EP buffer from Astoria
		 */
		cy_service_e_p_dma_read_request(dev_p, service_ep);
	}
#ifndef WESTBRIDGE_NDEBUG
	else
		cy_as_hal_print_message("cyashalomap:interrupt,"
					" w/o pending DMA job,"
					"-check DRQ_MASK logic\n");
#endif

	/*
	 * Now bump the EP ahead, so other endpoints get
	 * a shot before the one we just serviced
	 */
	if (end_points[service_ep].type == cy_as_hal_none) {
		if (service_ep == 15)
			service_ep = 2;
		else
			service_ep++;
	}

}

void cy_as_hal_dma_cancel_request(cy_as_hal_device_tag tag, uint8_t ep)
{
	DBGPRN("cy_as_hal_dma_cancel_request on ep:%d", ep);
	if (end_points[ep].pending)
		cy_as_hal_write_register(tag,
				CY_AS_MEM_P0_EP2_DMA_REG + ep - 2, 0);

	end_points[ep].buffer_valid = cy_false;
	end_points[ep].type = cy_as_hal_none;
}

/*
 * enables/disables SG list assisted DMA xfers for the given EP
 * sg_list assisted XFERS can use physical addresses of mem pages in case if the
 * xfer is performed by a h/w DMA controller rather then the CPU on P port
 */
void cy_as_hal_set_ep_dma_mode(uint8_t ep, bool sg_xfer_enabled)
{
	end_points[ep].sg_list_enabled = sg_xfer_enabled;
	DBGPRN("<1> EP:%d sg_list assisted DMA mode set to = %d\n",
			ep, end_points[ep].sg_list_enabled);
}
EXPORT_SYMBOL(cy_as_hal_set_ep_dma_mode);

/*
 * This function must be defined to transfer a block of data to
 * the WestBridge device.  This function can use the burst write
 * (DMA) capabilities of WestBridge to do this, or it can just copy
 * the data using writes.
 */
void cy_as_hal_dma_setup_write(cy_as_hal_device_tag tag,
						uint8_t ep, void *buf,
						uint32_t size, uint16_t maxsize)
{
	uint32_t addr = 0;
	uint16_t v  = 0;

	/*
	 * Note: "size" is the actual request size
	 * "maxsize" - is the P port fragment size
	 * No EP0 or EP1 traffic should get here
	 */
	cy_as_hal_assert(ep != 0 && ep != 1);

	/*
	 * If this asserts, we have an ordering problem.  Another DMA request
	 * is coming down before the previous one has completed.
	 */
	cy_as_hal_assert(end_points[ep].buffer_valid == cy_false);
	end_points[ep].buffer_valid = cy_true;
	end_points[ep].type = cy_as_hal_write;
	end_points[ep].pending = cy_true;

	/*
	 * total length of the request
	 */
	end_points[ep].req_length = size;

	if (size >= maxsize) {
		/*
		 * set xfer size for very 1st DMA xfer operation
		 * port max packet size ( typically 512 or 1024)
		 */
		end_points[ep].dma_xfer_sz = maxsize;
	} else {
		/*
		 * smaller xfers for non-storage EPs
		 */
		end_points[ep].dma_xfer_sz = size;
	}

	/*
	 * check the EP transfer mode uses sg_list rather then a memory buffer
	 * block devices pass it to the HAL, so the hAL could get to the real
	 * physical address for each segment and set up a DMA controller
	 * hardware ( if there is one)
	 */
	if (end_points[ep].sg_list_enabled) {
		/*
		 * buf -  pointer to the SG list
		 * data_p - data pointer to the 1st DMA segment
		 * seg_xfer_cnt - keeps track of N of bytes sent in current
		 *		sg_list segment
		 * req_xfer_cnt - keeps track of the total N of bytes
		 *		transferred for the request
		 */
		end_points[ep].sg_p = buf;
		end_points[ep].data_p = sg_virt(end_points[ep].sg_p);
		end_points[ep].seg_xfer_cnt = 0;
		end_points[ep].req_xfer_cnt = 0;

#ifdef DBGPRN_DMA_SETUP_WR
		DBGPRN("cyasomaphal:%s: EP:%d, buf:%p, buf_va:%p,"
				"req_sz:%d, maxsz:%d\n",
				__func__,
				ep,
				buf,
				end_points[ep].data_p,
				size,
				maxsize);
#endif

	} else {
		/*
		 * setup XFER for non sg_list assisted EPs
		 */

		#ifdef DBGPRN_DMA_SETUP_WR
			DBGPRN("<1>%s non storage or sz < 512:"
					"EP:%d, sz:%d\n", __func__, ep, size);
		#endif

		end_points[ep].sg_p = NULL;

		/*
		 * must be a VMA of a membuf in kernel space
		 */
		end_points[ep].data_p = buf;

		/*
		 * will keep track No of bytes xferred for the request
		 */
		end_points[ep].req_xfer_cnt = 0;
	}

	/*
	 * Tell WB we are ready to send data on the given endpoint
	 */
	v = (end_points[ep].dma_xfer_sz & CY_AS_MEM_P0_E_pn_DMA_REG_COUNT_MASK)
			| CY_AS_MEM_P0_E_pn_DMA_REG_DMAVAL;

	addr = CY_AS_MEM_P0_EP2_DMA_REG + ep - 2;

	cy_as_hal_write_register(tag, addr, v);
}

/*
 * This function must be defined to transfer a block of data from
 * the WestBridge device.  This function can use the burst read
 * (DMA) capabilities of WestBridge to do this, or it can just
 * copy the data using reads.
 */
void cy_as_hal_dma_setup_read(cy_as_hal_device_tag tag,
					uint8_t ep, void *buf,
					uint32_t size, uint16_t maxsize)
{
	uint32_t addr;
	uint16_t v;

	/*
	 * Note: "size" is the actual request size
	 * "maxsize" - is the P port fragment size
	 * No EP0 or EP1 traffic should get here
	 */
	cy_as_hal_assert(ep != 0 && ep != 1);

	/*
	 * If this asserts, we have an ordering problem.
	 * Another DMA request is coming down before the
	 * previous one has completed. we should not get
	 * new requests if current is still in process
	 */

	cy_as_hal_assert(end_points[ep].buffer_valid == cy_false);

	end_points[ep].buffer_valid = cy_true;
	end_points[ep].type = cy_as_hal_read;
	end_points[ep].pending = cy_true;
	end_points[ep].req_xfer_cnt = 0;
	end_points[ep].req_length = size;

	if (size >= maxsize) {
		/*
		 * set xfer size for very 1st DMA xfer operation
		 * port max packet size ( typically 512 or 1024)
		 */
		end_points[ep].dma_xfer_sz = maxsize;
	} else {
		/*
		 * so that we could handle small xfers on in case
		 * of non-storage EPs
		 */
		end_points[ep].dma_xfer_sz = size;
	}

	addr = CY_AS_MEM_P0_EP2_DMA_REG + ep - 2;

	if (end_points[ep].sg_list_enabled) {
		/*
		 * Handle sg-list assisted EPs
		 * seg_xfer_cnt - keeps track of N of sent packets
		 * buf - pointer to the SG list
		 * data_p - data pointer for the 1st DMA segment
		 */
		end_points[ep].seg_xfer_cnt = 0;
		end_points[ep].sg_p = buf;
		end_points[ep].data_p = sg_virt(end_points[ep].sg_p);

		#ifdef DBGPRN_DMA_SETUP_RD
		DBGPRN("cyasomaphal:DMA_setup_read sg_list EP:%d, "
			   "buf:%p, buf_va:%p, req_sz:%d, maxsz:%d\n",
				ep,
				buf,
				end_points[ep].data_p,
				size,
				maxsize);
		#endif
		v = (end_points[ep].dma_xfer_sz &
				CY_AS_MEM_P0_E_pn_DMA_REG_COUNT_MASK) |
				CY_AS_MEM_P0_E_pn_DMA_REG_DMAVAL;
		cy_as_hal_write_register(tag, addr, v);
	} else {
		/*
		 * Non sg list EP passed  void *buf rather then scatterlist *sg
		 */
		#ifdef DBGPRN_DMA_SETUP_RD
			DBGPRN("%s:non-sg_list EP:%d,"
					"RQ_sz:%d, maxsz:%d\n",
					__func__, ep, size,  maxsize);
		#endif

		end_points[ep].sg_p = NULL;

		/*
		 * must be a VMA of a membuf in kernel space
		 */
		end_points[ep].data_p = buf;

		/*
		 * Program the EP DMA register for Storage endpoints only.
		 */
		if (is_storage_e_p(ep)) {
			v = (end_points[ep].dma_xfer_sz &
					CY_AS_MEM_P0_E_pn_DMA_REG_COUNT_MASK) |
					CY_AS_MEM_P0_E_pn_DMA_REG_DMAVAL;
			cy_as_hal_write_register(tag, addr, v);
		}
	}
}

/*
 * This function must be defined to allow the WB API to
 * register a callback function that is called when a
 * DMA transfer is complete.
 */
void cy_as_hal_dma_register_callback(cy_as_hal_device_tag tag,
					cy_as_hal_dma_complete_callback cb)
{
	DBGPRN("<1>\n%s: WB API has registered a dma_complete callback:%x\n",
			__func__, (uint32_t)cb);
	callback = cb;
}

/*
 * This function must be defined to return the maximum size of
 * DMA request that can be handled on the given endpoint.  The
 * return value should be the maximum size in bytes that the DMA
 * module can handle.
 */
uint32_t cy_as_hal_dma_max_request_size(cy_as_hal_device_tag tag,
					cy_as_end_point_number_t ep)
{
	/*
	 * Storage reads and writes are always done in 512 byte blocks.
	 * So, we do the count handling within the HAL, and save on
	 * some of the data transfer delay.
	 */
	if ((ep == CYASSTORAGE_READ_EP_NUM) ||
	(ep == CYASSTORAGE_WRITE_EP_NUM)) {
		/* max DMA request size HAL can handle by itself */
		return CYASSTORAGE_MAX_XFER_SIZE;
	} else {
	/*
	 * For the USB - Processor endpoints, the maximum transfer
	 * size depends on the speed of USB operation. So, we use
	 * the following constant to indicate to the API that
	 * splitting of the data into chunks less that or equal to
	 * the max transfer size should be handled internally.
	 */

		/* DEFINED AS 0xffffffff in cyasdma.h */
		return CY_AS_DMA_MAX_SIZE_HW_SIZE;
	}
}

/*
 * This function must be defined to set the state of the WAKEUP pin
 * on the WestBridge device.  Generally this is done via a GPIO of
 * some type.
 */
cy_bool cy_as_hal_set_wakeup_pin(cy_as_hal_device_tag tag, cy_bool state)
{
	/*
	 * Not supported as of now.
	 */
	return cy_false;
}

void cy_as_hal_pll_lock_loss_handler(cy_as_hal_device_tag tag)
{
	cy_as_hal_print_message("error: astoria PLL lock is lost\n");
	cy_as_hal_print_message("please check the input voltage levels");
	cy_as_hal_print_message("and clock, and restart the system\n");
}

/*
 * Below are the functions that must be defined to provide the basic
 * operating system services required by the API.
 */

/*
 * This function is required by the API to allocate memory.
 * This function is expected to work exactly like malloc().
 */
void *cy_as_hal_alloc(uint32_t cnt)
{
	return kmalloc(cnt, GFP_ATOMIC);
}

/*
 * This function is required by the API to free memory allocated
 * with CyAsHalAlloc().  This function is'expected to work exacly
 * like free().
 */
void cy_as_hal_free(void *mem_p)
{
	kfree(mem_p);
}

/*
 * Allocator that can be used in interrupt context.
 * We have to ensure that the kmalloc call does not
 * sleep in this case.
 */
void *cy_as_hal_c_b_alloc(uint32_t cnt)
{
	return kmalloc(cnt, GFP_ATOMIC);
}

/*
 * This function is required to set a block of memory to a
 * specific value.  This function is expected to work exactly
 * like memset()
 */
void cy_as_hal_mem_set(void *ptr, uint8_t value, uint32_t cnt)
{
	memset(ptr, value, cnt);
}

/*
 * This function is expected to create a sleep channel.
 * The data structure that represents the sleep channel object
 * sleep channel (which is Linux "wait_queue_head_t wq" for this paticular HAL)
 * passed as a pointer, and allpocated by the caller
 * (typically as a local var on the stack) "Create" word should read as
 * "SleepOn", this func doesn't actually create anything
 */
cy_bool cy_as_hal_create_sleep_channel(cy_as_hal_sleep_channel *channel)
{
	init_waitqueue_head(&channel->wq);
	return cy_true;
}

/*
 * for this particular HAL it doesn't actually destroy anything
 * since no actual sleep object is created in CreateSleepChannel()
 * sleep channel is given by the pointer in the argument.
 */
cy_bool cy_as_hal_destroy_sleep_channel(cy_as_hal_sleep_channel *channel)
{
	return cy_true;
}

/*
 * platform specific wakeable Sleep implementation
 */
cy_bool cy_as_hal_sleep_on(cy_as_hal_sleep_channel *channel, uint32_t ms)
{
	wait_event_interruptible_timeout(channel->wq, 0, ((ms * HZ)/1000));
	return cy_true;
}

/*
 * wakes up the process waiting on the CHANNEL
 */
cy_bool cy_as_hal_wake(cy_as_hal_sleep_channel *channel)
{
	wake_up_interruptible_all(&channel->wq);
	return cy_true;
}

uint32_t cy_as_hal_disable_interrupts()
{
	if (0 == intr__enable)
		;

	intr__enable++;
	return 0;
}

void cy_as_hal_enable_interrupts(uint32_t val)
{
	intr__enable--;
	if (0 == intr__enable)
		;
}

/*
 * Sleep atleast 150ns, cpu dependent
 */
void cy_as_hal_sleep150(void)
{
	uint32_t i, j;

	j = 0;
	for (i = 0; i < 1000; i++)
		j += (~i);
}

void cy_as_hal_sleep(uint32_t ms)
{
	cy_as_hal_sleep_channel channel;

	cy_as_hal_create_sleep_channel(&channel);
	cy_as_hal_sleep_on(&channel, ms);
	cy_as_hal_destroy_sleep_channel(&channel);
}

cy_bool cy_as_hal_is_polling()
{
	return cy_false;
}

void cy_as_hal_c_b_free(void *ptr)
{
	cy_as_hal_free(ptr);
}

/*
 * suppose to reinstate the astoria registers
 * that may be clobbered in sleep mode
 */
void cy_as_hal_init_dev_registers(cy_as_hal_device_tag tag,
					cy_bool is_standby_wakeup)
{
	/* specific to SPI, no implementation required */
	(void) tag;
	(void) is_standby_wakeup;
}

void cy_as_hal_read_regs_before_standby(cy_as_hal_device_tag tag)
{
	/* specific to SPI, no implementation required */
	(void) tag;
}

cy_bool cy_as_hal_sync_device_clocks(cy_as_hal_device_tag tag)
{
	/*
	 * we are in asynchronous mode. so no need to handle this
	 */
	return true;
}

/*
 * init OMAP h/w resources
 */
int start_o_m_a_p_kernel(const char *pgm,
				cy_as_hal_device_tag *tag, cy_bool debug)
{
	cy_as_omap_dev_kernel *dev_p;
	int i;
	u16 data16[4];
	u8 pncfg_reg;

	/*
	 * No debug mode support through argument as of now
	 */
	(void)debug;

	DBGPRN(KERN_INFO"starting OMAP34xx HAL...\n");

	/*
	 * Initialize the HAL level endpoint DMA data.
	 */
	for (i = 0; i < sizeof(end_points)/sizeof(end_points[0]); i++) {
		end_points[i].data_p = 0;
		end_points[i].pending = cy_false;
		end_points[i].size = 0;
		end_points[i].type = cy_as_hal_none;
		end_points[i].sg_list_enabled = cy_false;

		/*
		 * by default the DMA transfers to/from the E_ps don't
		 * use sg_list that implies that the upper devices like
		 * blockdevice have to enable it for the E_ps in their
		 * initialization code
		 */
	}

	/*
	 * allocate memory for OMAP HAL
	 */
	dev_p = (cy_as_omap_dev_kernel *)cy_as_hal_alloc(
						sizeof(cy_as_omap_dev_kernel));
	if (dev_p == 0) {
		cy_as_hal_print_message("out of memory allocating OMAP"
					"device structure\n");
		return 0;
	}

	dev_p->m_sig = CY_AS_OMAP_KERNEL_HAL_SIG;

	/*
	 * initialize OMAP hardware and StartOMAPKernelall gpio pins
	 */
	dev_p->m_addr_base = (void *)cy_as_hal_processor_hw_init();

	/*
	 * Now perform a hard reset of the device to have
	 * the new settings take effect
	 */
	__gpio_set_value(AST_WAKEUP, 1);

	/*
	 * do Astoria  h/w reset
	 */
	DBGPRN(KERN_INFO"-_-_pulse -> westbridge RST pin\n");

	/*
	 * NEGATIVE PULSE on RST pin
	 */
	__gpio_set_value(AST_RESET, 0);
	mdelay(1);
	__gpio_set_value(AST_RESET, 1);
	mdelay(50);

	/*
	* note AFTER reset PNAND interface is 8 bit mode
	* so if gpmc Is configured in 8 bit mode upper half will be FF
	*/
	pncfg_reg = ast_p_nand_casdo_read(CY_AS_MEM_PNAND_CFG);

#ifdef PNAND_16BIT_MODE

	/*
	 * switch to 16 bit mode, force NON-LNA LBD mode, 3 RA addr bytes
	 */
	ast_p_nand_casdi_write(CY_AS_MEM_PNAND_CFG, 0x0001);

	/*
	 * now in order to continue to talk to astoria
	 * sw OMAP GPMC into 16 bit mode as well
	 */
	cy_as_hal_gpmc_enable_16bit_bus(cy_true);
#else
   /* Astoria and GPMC are already in 8 bit mode, jsut initialize PNAND_CFG */
	ast_p_nand_casdi_write(CY_AS_MEM_PNAND_CFG, 0x0000);
#endif

   /*
	*  NOTE: if you want to capture bus activity on the LA,
	*  don't use printks in between the activities you want to capture.
	*  prinks may take milliseconds, and the data of interest
	*  will fall outside the LA capture window/buffer
	*/
	data16[0] = ast_p_nand_casdo_read(CY_AS_MEM_CM_WB_CFG_ID);
	data16[1] = ast_p_nand_casdo_read(CY_AS_MEM_PNAND_CFG);

	if (data16[0] != 0xA200) {
		/*
		 * astoria device is not found
		 */
		printk(KERN_ERR "ERROR: astoria device is not found, CY_AS_MEM_CM_WB_CFG_ID ");
		printk(KERN_ERR "read returned:%4.4X: CY_AS_MEM_PNAND_CFG:%4.4x !\n",
				data16[0], data16[0]);
		goto bus_acc_error;
	}

	cy_as_hal_print_message(KERN_INFO" register access CASDO test:"
				"\n CY_AS_MEM_CM_WB_CFG_ID:%4.4x\n"
				"PNAND_CFG after RST:%4.4x\n "
				"CY_AS_MEM_PNAND_CFG"
				"after cfg_wr:%4.4x\n\n",
				data16[0], pncfg_reg, data16[1]);

	dev_p->thread_flag = 1;
	spin_lock_init(&int_lock);
	dev_p->m_next_p = m_omap_list_p;

	m_omap_list_p = dev_p;
	*tag = dev_p;

	cy_as_hal_configure_interrupts((void *)dev_p);

	cy_as_hal_print_message(KERN_INFO"OMAP3430__hal started tag:%p"
				", kernel HZ:%d\n", dev_p, HZ);

	/*
	 *make processor to storage endpoints SG assisted by default
	 */
	cy_as_hal_set_ep_dma_mode(4, true);
	cy_as_hal_set_ep_dma_mode(8, true);

	return 1;

	/*
	 * there's been a NAND bus access error or
	 * astoria device is not connected
	 */
bus_acc_error:
	/*
	 * at this point hal tag hasn't been set yet
	 * so the device will not call omap_stop
	 */
	cy_as_hal_omap_hardware_deinit(dev_p);
	cy_as_hal_free(dev_p);
	return 0;
}

#else
/*
 * Some compilers do not like empty C files, so if the OMAP hal is not being
 * compiled, we compile this single function.  We do this so that for a
 * given target HAL there are not multiple sources for the HAL functions.
 */
void my_o_m_a_p_kernel_hal_dummy_function(void)
{
}

#endif
