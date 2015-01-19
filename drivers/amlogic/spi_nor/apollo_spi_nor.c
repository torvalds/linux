#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <mach/am_regs.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <plat/io.h>
#include <linux/of.h>

#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <mach/spi_nor.h>
#include <linux/mtd/partitions.h>
#include <mach/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <mach/pinmux_queue.h>
#include <linux/list.h>
#include <mach/mod_gate.h>

struct amlogic_spi_user_crtl {
	unsigned char	user_def_cmd;
	unsigned char	cmd_have_addr;
	unsigned char	cmd_have_dummy;
	unsigned char	cmd_have_data_in;
	unsigned char	cmd_have_data_out;
	unsigned char	cmd_dummy_num;
	unsigned char	user_def_cmd_value;
	unsigned		addr;
	int 		tx_data_len;
	int 		rx_data_len;
	unsigned char	*data_buf;
};

struct amlogic_spi {
	spinlock_t		lock;
	struct list_head	msg_queue;
	struct spi_master	*master;
	struct spi_device	spi_dev;

	unsigned char		*map_base;
	unsigned char		*map_end;
#ifdef CONFIG_OF
	struct device *dev;
	struct pinctrl *p;
	char *spi_state_name;
	struct pinctrl_state *spi_state;
	struct pinctrl_state *spi_idlestate;
#endif
};

//static bool spi_chip_select(bool flag);
//static DEFINE_SPINLOCK(pinmux_set_lock);

#if (defined(CONFIG_ARCH_MESON6) || defined(CONFIG_ARCH_MESON8) || defined(CONFIG_ARCH_MESON8B))
#if 0
static pinmux_item_t spi_nor_set_pins[] ={
	{
		.reg = PINMUX_REG(5),
		.setmask = 0xf,
    },
  {
  	.reg = PINMUX_REG(2),
  	.clrmask = ((1<<19)|(1<<20)|(1<<21)),
  },
    PINMUX_END_ITEM
};

static pinmux_item_t spi_nor_clr_pins[] ={
	{
    	.reg = PINMUX_REG(5),
    	.setmask = 0xf,
    },
  {
    .reg = PINMUX_REG(2),
  	.clrmask = ((1<<19)|(1<<20)|(1<<21)),
  },
    PINMUX_END_ITEM
};

static pinmux_set_t spi_nor_set = {
	.chip_select = NULL,
	.pinmux = &spi_nor_set_pins[0],
};

static pinmux_set_t spi_nor_clr = {
	.chip_select = spi_chip_select,
	.pinmux = &spi_nor_clr_pins[0],
};
#endif

#elif defined(CONFIG_ARCH_MESON3)
static pinmux_item_t spi_nor_pins[] ={
	{
		.reg = PINMUX_REG(5),
		.clrmask = ((1<<6)|(1<<7)|(1<<8)|(1<<9)),
		.setmask = ((1<<0) | (1<<1) | (1<<2) | (1<<3)),
    },
    {
    	.reg = PINMUX_REG(2),
    	.clrmask = ((1<<19)|(1<<20)|(1<<21)),
    },
    PINMUX_END_ITEM
};

static pinmux_set_t spi_nor_set = {
	.chip_select = NULL,
	.pinmux = &spi_nor_pins[0],
};
#endif

#if 0
static bool spi_chip_select(bool flag)
{
	return flag;
}
#endif
static void spi_hw_init(struct amlogic_spi	*amlogic_spi)
{
#if  defined(ONFIG_AMLOGIC_BOARD_APOLLO) || defined(CONFIG_AMLOGIC_BOARD_APOLLO_H)
	CLEAR_PERI_REG_MASK(PREG_PIN_MUX_REG4, ((1 << 1)|(1 << 2)|(1 << 4)|(1 << 5)|(1 << 6)));
	if (((READ_MPEG_REG(ASSIST_POR_CONFIG) & (1 << 1)) ? 1 : 0) && ((READ_MPEG_REG(ASSIST_POR_CONFIG) & (1 << 6)) ? 1 : 0)) {
		amlogic_spi->map_base = (unsigned char *)(0xC1800000);
		amlogic_spi->map_end = (unsigned char *)(0xC1FFFFFF);
	}
	else {
		amlogic_spi->map_base = (unsigned char *)(0x00000000);
		amlogic_spi->map_end = (unsigned char *)(0x00FFFFFF);
	}
#endif
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	//WRITE_PERI_REG(PREG_SPI_FLASH_CTRL, 0xea525);
	aml_write_reg32(P_SPI_FLASH_CTRL, 0xea949);
	//WRITE_PERI_REG(PREG_SPI_FLASH_CTRL1, 0xf0280100);
	aml_write_reg32(P_SPI_FLASH_CTRL1, 0xf0280100);
	//SET_PERI_REG_MASK(SPI_FLASH_USER,(1<<2));
	aml_set_reg32_mask(P_SPI_FLASH_USER, (1<<2));
}
 int xx_spi=0;
static void spi_hw_enable(struct amlogic_spi	*amlogic_spi)
{
int retry = 0,ret;
	//DECLARE_WAITQUEUE(spi_wait, current);
#if (defined(CONFIG_ARCH_MESON3) || defined(CONFIG_ARCH_MESON6) ||defined(CONFIG_ARCH_MESON8) || defined(CONFIG_ARCH_MESON8B))
  /*clear_mio_mux(2,7<<19);
  clear_mio_mux(5,(0xf<<6));
	set_mio_mux(5, 0xf);*/
	/*aml_clr_reg32_mask(P_PERIPHS_PIN_MUX_2, ((1<<19)|(1<<20)|(1<<21)));
	aml_clr_reg32_mask(P_PERIPHS_PIN_MUX_5, ((1<<6)|(1<<7)|(1<<8)|(1<<9)));
	aml_set_reg32_mask(P_PERIPHS_PIN_MUX_5,((1<<0) | (1<<1) | (1<<2) | (1<<3)));	*/
	/*#elif defined(CONFIG_ARCH_MESON6)*/
	// can't set/clr pinmux directly
	//aml_clr_reg32_mask(P_PERIPHS_PIN_MUX_2, ((1<<19)|(1<<20)|(1<<21)));
	//aml_set_reg32_mask(P_PERIPHS_PIN_MUX_5, ((1<<0) | (1<<1) | (1<<2) | (1<<3)));
	#ifdef CONFIG_OF
	for (retry=0; retry<10; retry++) {
		mutex_lock(&spi_nand_mutex);
		ret =  pinctrl_select_state(amlogic_spi->p, amlogic_spi->spi_state);
		if(ret<0){
			mutex_unlock(&spi_nand_mutex);
			printk("set spi pinmux error\n");
		}
		else break;
	}
	if (retry == 10) return;
		//printk("P_PERIPHS_PIN_MUX_2 = %x\n", aml_read_reg32(P_PERIPHS_PIN_MUX_2));
		//printk("P_PERIPHS_PIN_MUX_5 = %x\n", aml_read_reg32(P_PERIPHS_PIN_MUX_5));
	#else
		#if
	pinmux_set(&spi_nor_set);
#else
	clear_mio_mux(6,0x7fff);
	SET_PERI_REG_MASK(PERIPHS_PIN_MUX_1, ((1 << 23)|(1 <<25)|(1 << 27)|(1 << 29)));
#endif
	#endif
#endif
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
}

static void spi_hw_disable(struct amlogic_spi	*amlogic_spi)
{
#if (defined(CONFIG_ARCH_MESON6) || defined(CONFIG_ARCH_MESON8) || defined(CONFIG_ARCH_MESON8B))
#ifdef CONFIG_OF
	int ret=0;
	if(amlogic_spi->p)
	{	
		ret = pinctrl_select_state(amlogic_spi->p, amlogic_spi->spi_idlestate);
		
		if(ret<0)
			printk("select idle state error\n");
		mutex_unlock(&spi_nand_mutex);
	}
#else
	pinmux_clr(&spi_nor_clr);	
#endif	
#elif defined(CONFIG_ARCH_MESON3)
  pinmux_clr(&spi_nor_set);
#else
	CLEAR_PERI_REG_MASK(PERIPHS_PIN_MUX_1, ((1 << 23)|(1 <<25)|(1 << 27)|(1 << 29)));
#endif
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
}

static int spi_add_dev(struct amlogic_spi *amlogic_spi, struct spi_master	*master)
{
	amlogic_spi->spi_dev.master = master;
	device_initialize(&amlogic_spi->spi_dev.dev);
	amlogic_spi->spi_dev.dev.parent = &amlogic_spi->master->dev;
	amlogic_spi->spi_dev.dev.bus = &spi_bus_type;
	amlogic_spi->spi_dev.dev.of_node = amlogic_spi->dev->of_node;

	strcpy((char *)amlogic_spi->spi_dev.modalias, SPI_DEV_NAME);
	dev_set_name(&amlogic_spi->spi_dev.dev, "%s:%d", "apollospi", master->bus_num);
	return device_add(&amlogic_spi->spi_dev.dev);
}

static  void amlogic_spi_cleanup(struct spi_device	*spi)
{
	if (spi->modalias)
		kfree(spi->modalias);
}

static int spi_receive_cycle(struct amlogic_spi	*amlogic_spi, struct amlogic_spi_user_crtl *amlogic_user_spi)
{
	unsigned spi_cmd_reg = ((amlogic_user_spi->user_def_cmd<<SPI_FLASH_USR) |
							(amlogic_user_spi->cmd_have_addr<<SPI_FLASH_USR_ADDR) |
							(amlogic_user_spi->cmd_have_dummy<<SPI_FLASH_USR_DUMMY) |
							(amlogic_user_spi->cmd_have_data_in<<SPI_FLASH_USR_DIN) |
							(amlogic_user_spi->cmd_have_data_out<<SPI_FLASH_USR_DOUT) |
							(amlogic_user_spi->cmd_dummy_num<<SPI_FLASH_USR_DUMMY_BLEN) |
							(amlogic_user_spi->user_def_cmd_value<<SPI_FLASH_USR_CMD));
	unsigned read_addr = amlogic_user_spi->addr;
	int spi_rx_len = amlogic_user_spi->rx_data_len;
	unsigned *data_buf = (unsigned *)amlogic_user_spi->data_buf;
	int i, read_len;
	unsigned temp_buf[8], data_offset = 0;

	if (amlogic_user_spi->user_def_cmd == 0) {
		//don`t know why memcpy from the nor map failed
		//CLEAR_PERI_REG_MASK(PREG_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
		aml_clr_reg32_mask(P_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
		/*if ((read_addr + spi_rx_len) <= (unsigned)amlogic_spi->map_end) {
			memcpy(amlogic_user_spi->data_buf, (amlogic_spi->map_base+read_addr), spi_rx_len);
			SET_PERI_REG_MASK(PREG_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
			return 0;
		}*/

		while (spi_rx_len > 0 )
		{
			if(spi_rx_len >= 32)
				read_len = 32;
			else
				read_len = spi_rx_len;

			switch (amlogic_user_spi->user_def_cmd_value) {
				case OPCODE_RDID:
					/*WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_RDID));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);*/
					aml_write_reg32(P_SPI_FLASH_CMD,(1<<SPI_FLASH_RDID));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);
					break;

				case OPCODE_RDSR:
					/*WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_RDSR));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);*/
					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_RDSR));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);
					break;

				case OPCODE_NORM_READ:
					/*WRITE_PERI_REG(PREG_SPI_FLASH_ADDR, ((read_addr & 0xffffff)|(read_len << SPI_FLASH_BYTES_LEN)));
					WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_READ));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);*/
					aml_write_reg32(P_SPI_FLASH_ADDR, ((read_addr & 0xffffff)|(read_len << SPI_FLASH_BYTES_LEN)));
					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_READ));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);
					break;

				default:
					break;
			}

			for (i=0; i<8; i++) {
				if (spi_rx_len <= 0)
					break;
				if (amlogic_user_spi->user_def_cmd_value == OPCODE_RDSR)
					//*(temp_buf+i) = READ_PERI_REG(PREG_SPI_FLASH_STATUS);
					*(temp_buf+i) = aml_read_reg32(P_SPI_FLASH_STATUS);
				else
					//*(temp_buf+i) = READ_PERI_REG(PREG_SPI_FLASH_C0+i);
					*(temp_buf+i) = aml_read_reg32(P_SPI_FLASH_C0+i*4);
				spi_rx_len -= 4;
			}
			memcpy((unsigned char *)data_buf+data_offset, (unsigned char *)temp_buf, read_len);
			data_offset += read_len;
			read_addr += read_len;
		}
		//SET_PERI_REG_MASK(PREG_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
		aml_set_reg32_mask(P_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
	}
	else {
		//CLEAR_PERI_REG_MASK(PREG_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
		aml_clr_reg32_mask(P_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
		while (spi_rx_len > 0 )
		{
			if(spi_rx_len >= 32)
				read_len = 32;
			else
				read_len = spi_rx_len;

			/*WRITE_PERI_REG(PREG_SPI_FLASH_ADDR, (read_len << SPI_FLASH_BYTES_LEN));
			WRITE_PERI_REG(PREG_SPI_FLASH_CMD, spi_cmd_reg);*/
			aml_write_reg32(P_SPI_FLASH_ADDR, (read_len << SPI_FLASH_BYTES_LEN));
			aml_write_reg32(P_SPI_FLASH_CMD, spi_cmd_reg);
			udelay(1);

			for (i=0; i<8; i++) {
				if (spi_rx_len <= 0)
					break;
				//*(temp_buf+i) = READ_PERI_REG(PREG_SPI_FLASH_C0+i);
				*(temp_buf+i) = aml_read_reg32(P_SPI_FLASH_C0+i*4);
				spi_rx_len -= 4;
			}
			memcpy((unsigned char *)data_buf+data_offset, (unsigned char *)temp_buf, read_len);
			data_offset += read_len;
		}
		//SET_PERI_REG_MASK(PREG_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
		aml_set_reg32_mask(P_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
	}

	return 0;
}

static int spi_transmit_cycle(struct amlogic_spi *amlogic_spi, struct amlogic_spi_user_crtl *amlogic_user_spi)
{
	unsigned spi_cmd_reg = ((amlogic_user_spi->user_def_cmd<<SPI_FLASH_USR) |
							(amlogic_user_spi->cmd_have_addr<<SPI_FLASH_USR_ADDR) |
							(amlogic_user_spi->cmd_have_dummy<<SPI_FLASH_USR_DUMMY) |
							(amlogic_user_spi->cmd_have_data_in<<SPI_FLASH_USR_DIN) |
							(amlogic_user_spi->cmd_have_data_out<<SPI_FLASH_USR_DOUT) |
							(amlogic_user_spi->cmd_dummy_num<<SPI_FLASH_USR_DUMMY_BLEN) |
							(amlogic_user_spi->user_def_cmd_value<<SPI_FLASH_USR_CMD));
	unsigned write_addr = amlogic_user_spi->addr;
	int spi_tx_len = amlogic_user_spi->tx_data_len;
	unsigned *data_buf = (unsigned *)amlogic_user_spi->data_buf;
	int i, temp, write_len;
	unsigned temp_buf[8], data_offset = 0;

	//CLEAR_PERI_REG_MASK(PREG_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
	aml_clr_reg32_mask(P_SPI_FLASH_CTRL, SPI_ENABLE_AHB);

	if (amlogic_user_spi->user_def_cmd == 0) {
		do {
			if (spi_tx_len > 32)
				write_len = 32;
			else
				write_len = spi_tx_len;

			memcpy((unsigned char *)temp_buf, (unsigned char *)data_buf+data_offset, write_len);
			for (i=0; i<write_len; i+=4) {
				if (spi_tx_len <= 0)
					break;
				if (amlogic_user_spi->user_def_cmd_value == OPCODE_WRSR)
					//WRITE_PERI_REG(PREG_SPI_FLASH_STATUS, (*(temp_buf+i/4) & 0xff));
					aml_write_reg32(P_SPI_FLASH_STATUS, (*(temp_buf+i/4) & 0xff));
				else
					//WRITE_PERI_REG((PREG_SPI_FLASH_C0+i/4), *(temp_buf+i/4));
					//aml_write_reg32((P_SPI_FLASH_C0+i/4), *(temp_buf+i/4));
					aml_write_reg32((P_SPI_FLASH_C0+i), *(temp_buf+i/4));
			}

			switch (amlogic_user_spi->user_def_cmd_value) {
				case OPCODE_WREN:
					/*WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);*/
					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);
					break;

				case OPCODE_WRSR:
					/*WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);

					WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_WRSR));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);*/
					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);

					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_WRSR));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);
					break;

				case OPCODE_SE_4K:
					/*WRITE_PERI_REG(PREG_SPI_FLASH_ADDR, (write_addr & 0xffffff));
					WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);

					WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_SE));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);*/
					aml_write_reg32(P_SPI_FLASH_ADDR, (write_addr & 0xffffff));
					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);

					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_SE));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);
					break;

				case OPCODE_BE:
					/*WRITE_PERI_REG(PREG_SPI_FLASH_ADDR, (write_addr & 0xffffff));
					WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);

					WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_BE));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);*/
					aml_write_reg32(P_SPI_FLASH_ADDR, (write_addr & 0xffffff));
					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);

					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_BE));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);
					break;

				case OPCODE_PP:
					/*WRITE_PERI_REG(PREG_SPI_FLASH_ADDR, ((write_addr & 0xffffff) | (write_len << SPI_FLASH_BYTES_LEN )));
					WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(READ_PERI_REG(PREG_SPI_FLASH_CMD) != 0);

					WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_PP));
					while ( READ_PERI_REG(PREG_SPI_FLASH_CMD ) !=0 );*/
					aml_write_reg32(P_SPI_FLASH_ADDR, ((write_addr & 0xffffff) | (write_len << SPI_FLASH_BYTES_LEN )));
					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_WREN));
					while(aml_read_reg32(P_SPI_FLASH_CMD) != 0);

					aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_PP));
					while ( aml_read_reg32(P_SPI_FLASH_CMD ) !=0 );
					break;

				default:
					break;
			}

			temp = 1;
			while ( (temp & 1) == 1 )
			{
				/*WRITE_PERI_REG(PREG_SPI_FLASH_CMD, (1<<SPI_FLASH_RDSR));
				while ( READ_PERI_REG(PREG_SPI_FLASH_CMD ) !=0 );

				temp = READ_PERI_REG(PREG_SPI_FLASH_STATUS);*/
				aml_write_reg32(P_SPI_FLASH_CMD, (1<<SPI_FLASH_RDSR));
				while ( aml_read_reg32(P_SPI_FLASH_CMD ) !=0 );

				temp = aml_read_reg32(P_SPI_FLASH_STATUS);
			}
			write_addr += write_len;
			data_offset += write_len;
			spi_tx_len -= write_len;

		}while (spi_tx_len > 0 );
	}
	else {
		do {
			if(spi_tx_len >= 32)
				write_len = 32;
			else
				write_len = spi_tx_len;

			for (i=0; i<8; i++) {
				if (spi_tx_len <= 0)
					break;
				//WRITE_PERI_REG((PREG_SPI_FLASH_C0+i), *data_buf++);
				aml_write_reg32((P_SPI_FLASH_C0+i*4), *data_buf++);
				spi_tx_len -= 4;
			}

			/*WRITE_PERI_REG(PREG_SPI_FLASH_ADDR, (write_len << SPI_FLASH_BYTES_LEN));
			WRITE_PERI_REG(PREG_SPI_FLASH_CMD, spi_cmd_reg);*/
			aml_write_reg32(P_SPI_FLASH_ADDR, (write_len << SPI_FLASH_BYTES_LEN));
			aml_write_reg32(P_SPI_FLASH_CMD, spi_cmd_reg);
			udelay(10);
		}while (spi_tx_len > 0 );
	}

	//SET_PERI_REG_MASK(PREG_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
	aml_set_reg32_mask(P_SPI_FLASH_CTRL, SPI_ENABLE_AHB);
	return 0;
}


static int amlogic_spi_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct amlogic_spi	*amlogic_spi;
	struct amlogic_spi_user_crtl amlogic_hw_ctl;
	//unsigned long		flags;
	struct spi_transfer	*t;
	unsigned command_index = 0;
	amlogic_spi = spi_master_get_devdata(spi->master);
	spin_lock(&amlogic_spi->lock);
	m->actual_length = 0;
	m->status = 0;
	memset(&amlogic_hw_ctl, 0x0, sizeof(struct amlogic_spi_user_crtl));

	/* reject invalid messages and transfers */
	if (list_empty(&m->transfers) || !m->complete)
		return -EINVAL;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		unsigned char *tx_buf = (unsigned char *)t->tx_buf;
		unsigned char *rx_buf = (unsigned char *)t->rx_buf;
		unsigned	len = t->len;

		if (tx_buf != NULL) {
			if(command_index) {
				amlogic_hw_ctl.cmd_have_data_out = 1;
				amlogic_hw_ctl.tx_data_len = len;
				amlogic_hw_ctl.data_buf = tx_buf;
				m->actual_length += len;
			}
			else {
				amlogic_hw_ctl.user_def_cmd_value = tx_buf[0];
				//for distinguish user command or apollo spi hw already exist command
				if ((amlogic_hw_ctl.user_def_cmd_value == OPCODE_WREN) ||
					(amlogic_hw_ctl.user_def_cmd_value == OPCODE_RDSR) ||
					(amlogic_hw_ctl.user_def_cmd_value == OPCODE_NORM_READ) ||
					(amlogic_hw_ctl.user_def_cmd_value == OPCODE_PP) ||
					(amlogic_hw_ctl.user_def_cmd_value == OPCODE_SE_4K) ||
					(amlogic_hw_ctl.user_def_cmd_value == OPCODE_BE) ||
					(amlogic_hw_ctl.user_def_cmd_value == OPCODE_RDID) ||
					(amlogic_hw_ctl.user_def_cmd_value == OPCODE_WRSR) )
					amlogic_hw_ctl.user_def_cmd = 0;
				else
					amlogic_hw_ctl.user_def_cmd = 1;

				if ((len > 1) && (len < 4)) {
					amlogic_hw_ctl.tx_data_len = (len - 1);
					amlogic_hw_ctl.data_buf = &tx_buf[1];
				}
				else if (len == 4) {
					amlogic_hw_ctl.addr = (tx_buf[3] + (tx_buf[2]<<8) + (tx_buf[1]<<16))&0xffffff;
					amlogic_hw_ctl.cmd_have_addr = 1;
				}
				else if (len > 4) {
					amlogic_hw_ctl.cmd_have_dummy = 1;
					amlogic_hw_ctl.cmd_dummy_num = (len - 4);
				}
				command_index = 1;
				m->actual_length += len;
			}
		}
		if (rx_buf != NULL) {
			if(command_index) {
				amlogic_hw_ctl.cmd_have_data_in = 1;
				amlogic_hw_ctl.rx_data_len = len;
				amlogic_hw_ctl.data_buf = rx_buf;
				m->actual_length += len;
			}
		}
	}

	if ((amlogic_hw_ctl.rx_data_len > 0) && (amlogic_hw_ctl.tx_data_len > 0)) {
		printk("can`t do read and write simultaneous\n");
		BUG();
	    spin_unlock(&amlogic_spi->lock);
		return -1;
	}

#if (defined(CONFIG_ARCH_MESON6) || defined(CONFIG_ARCH_MESON8) || defined(CONFIG_ARCH_MESON8B))
	spin_unlock(&amlogic_spi->lock);
#endif
	//spin_lock_irqsave(&amlogic_spi->lock, flags);
	spi_hw_enable(amlogic_spi);
#if (defined(CONFIG_ARCH_MESON6) || defined(CONFIG_ARCH_MESON8) || defined(CONFIG_ARCH_MESON8B))
	spin_lock(&amlogic_spi->lock);
#endif
	if (amlogic_hw_ctl.cmd_have_data_in)
		spi_receive_cycle(amlogic_spi, &amlogic_hw_ctl);
	else
		spi_transmit_cycle(amlogic_spi, &amlogic_hw_ctl);

	spin_unlock(&amlogic_spi->lock);

	spi_hw_disable(amlogic_spi);

	return 0;
}

static int amlogic_spi_nor_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct amlogic_spi	*amlogic_spi;
	struct resource		*r;
	int			status = 0;
	unsigned		num_chipselect = 1;

	printk("%s:\n", __func__);
	master = spi_alloc_master(&pdev->dev, sizeof *amlogic_spi);
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	if (pdev->id != -1)
		master->bus_num = pdev->id;

	//master->setup = amlogic_spi_setup;
	master->transfer = amlogic_spi_transfer;
	master->cleanup = amlogic_spi_cleanup;
	master->num_chipselect = num_chipselect;

	dev_set_drvdata(&pdev->dev, master);

	amlogic_spi = spi_master_get_devdata(master);
	amlogic_spi->master = master;

#ifdef CONFIG_OF
		of_property_read_string(pdev->dev.of_node,"pinctrl-names",(const char **)&amlogic_spi->spi_state_name);
		printk("amlogic_spi->state_name:%s\n",amlogic_spi->spi_state_name);
#endif

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		status = -ENODEV;
		goto err1;
	}

	//amlogic_spi->map_base = (unsigned char *)(r->start);
	//amlogic_spi->map_end = (unsigned char *)(r->end);
	amlogic_spi->dev = &pdev->dev;
	amlogic_spi->spi_dev.dev.platform_data = pdev->dev.platform_data;
	spin_lock_init(&amlogic_spi->lock);
	INIT_LIST_HEAD(&amlogic_spi->msg_queue);
	amlogic_spi->p = devm_pinctrl_get(amlogic_spi->dev);
	if (IS_ERR(amlogic_spi->p))
		return PTR_ERR(amlogic_spi->p);
	amlogic_spi->spi_state=pinctrl_lookup_state(amlogic_spi->p,amlogic_spi->spi_state_name);
	if (IS_ERR(amlogic_spi->spi_state)) {
		pinctrl_put(amlogic_spi->p);
		return PTR_ERR(amlogic_spi->spi_state);
	}
	amlogic_spi->spi_idlestate=pinctrl_lookup_state(amlogic_spi->p,"dummy");
	if (IS_ERR(amlogic_spi->spi_idlestate)) {
		pinctrl_put(amlogic_spi->p);
		return PTR_ERR(amlogic_spi->spi_idlestate);
	}
	status = spi_register_master(master);
	if (status < 0)
		goto err1;
	switch_mod_gate_by_name("spi", 1);
	spi_hw_init(amlogic_spi);

	status = spi_add_dev(amlogic_spi, master);
	printk("%s over\n", __func__);
	return status;

err1:
	spi_master_put(master);
	return status;
}

static int amlogic_spi_nor_remove(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct amlogic_spi	*amlogic_spi;
	switch_mod_gate_by_name("spi", 0);
	master = dev_get_drvdata(&pdev->dev);
	amlogic_spi = spi_master_get_devdata(master);

	spi_unregister_master(master);

	return 0;
}

static const struct of_device_id amlogic_apollo_spi_nor_dt_match[]={
	{	.compatible = "amlogic,apollo_spi_nor",
	},
	{},
};

static struct platform_driver amlogic_spi_nor_driver = {
	.probe = amlogic_spi_nor_probe,
	.remove = amlogic_spi_nor_remove,
	.driver =
	    {
			.name = "AMLOGIC_SPI_NOR",
			.owner = THIS_MODULE,
			.of_match_table = amlogic_apollo_spi_nor_dt_match,
		},
};

static int __init amlogic_spi_nor_init(void)
{
	return platform_driver_register(&amlogic_spi_nor_driver);
}

static void __exit amlogic_spi_nor_exit(void)
{
	platform_driver_unregister(&amlogic_spi_nor_driver);
}

module_init(amlogic_spi_nor_init);
module_exit(amlogic_spi_nor_exit);

MODULE_DESCRIPTION("Amlogic Spi Nor Flash driver");
MODULE_LICENSE("GPL");

