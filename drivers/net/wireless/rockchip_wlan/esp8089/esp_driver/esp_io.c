/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *   IO interface 
 *    - sdio/spi common i/f driver
 *    - target sdio hal
 */

#include <linux/mmc/sdio_func.h>
#include "esp_sif.h"
#include "slc_host_register.h"
#include "esp_debug.h"

#ifdef SIF_DEBUG_DSR_DUMP_REG
static void dump_slc_regs(struct slc_host_regs *regs);
#endif /* SIF_DEBUG_DSR_DUMP_REG */

int esp_common_read(struct esp_pub *epub, u8 *buf, u32 len, int sync, bool noround)
{
	if (sync) {
#ifdef ESP_USE_SDIO
		return sif_lldesc_read_sync(epub, buf, len);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_read_sync(epub, buf, len, NOT_DUMMYMODE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		return sif_lldesc_read_raw(epub, buf, len, noround);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_read_nosync(epub, buf, len, NOT_DUMMYMODE, noround);
#endif
	}
}


int esp_common_write(struct esp_pub *epub, u8 *buf, u32 len, int sync)
{
	if (sync) {
#ifdef ESP_USE_SDIO
		return sif_lldesc_write_sync(epub, buf, len);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_write_sync(epub, buf, len, NOT_DUMMYMODE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		return sif_lldesc_write_raw(epub, buf, len);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_write_nosync(epub, buf, len, NOT_DUMMYMODE);
#endif
	}
}


int esp_common_read_with_addr(struct esp_pub *epub, u32 addr, u8 *buf, u32 len, int sync)
{
	if (sync) {
#ifdef ESP_USE_SDIO
		return sif_io_sync(epub, addr, buf, len, SIF_FROM_DEVICE | SIF_SYNC | SIF_BYTE_BASIS | SIF_INC_ADDR);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_read_mix_sync(epub, addr, buf, len, NOT_DUMMYMODE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		return sif_io_raw(epub, addr, buf, len, SIF_FROM_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_read_mix_nosync(epub, addr, buf, len, NOT_DUMMYMODE);
#endif
	}

}


int esp_common_write_with_addr(struct esp_pub *epub, u32 addr, u8 *buf, u32 len, int sync)
{
	if (sync) {
#ifdef ESP_USE_SDIO
		return sif_io_sync(epub, addr, buf, len, SIF_TO_DEVICE | SIF_SYNC | SIF_BYTE_BASIS | SIF_INC_ADDR);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_write_mix_sync(epub, addr, buf, len, NOT_DUMMYMODE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		return sif_io_raw(epub, addr, buf, len, SIF_TO_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_write_mix_nosync(epub, addr, buf, len, NOT_DUMMYMODE);
#endif
	}
}

int esp_common_readbyte_with_addr(struct esp_pub *epub, u32 addr, u8 *buf, int sync)
{
	if(sync){
#ifdef ESP_USE_SDIO
		int res;
		sif_lock_bus(epub);
		*buf = sdio_io_readb(epub, addr, &res);
		sif_unlock_bus(epub);
		return res;
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_read_mix_sync(epub, addr, buf, 1, NOT_DUMMYMODE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		int res;
		*buf = sdio_io_readb(epub, addr, &res);
		return res;
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_read_mix_nosync(epub, addr, buf, 1, NOT_DUMMYMODE);
#endif
	}

}



int esp_common_writebyte_with_addr(struct esp_pub *epub, u32 addr, u8 buf, int sync)
{
	if(sync){
#ifdef ESP_USE_SDIO
		int res;
		sif_lock_bus(epub);
		sdio_io_writeb(epub, buf, addr, &res);
		sif_unlock_bus(epub);
		return res;
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_write_mix_sync(epub, addr, &buf, 1, NOT_DUMMYMODE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		int res;
		sdio_io_writeb(epub, buf, addr, &res);
		return res;
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_write_mix_nosync(epub, addr, &buf, 1, NOT_DUMMYMODE);
#endif
	}
}

int sif_read_reg_window(struct esp_pub *epub, unsigned int reg_addr, u8 *value)
{
        u8 *p_tbuf = NULL;
	int ret = 0;
	int retry = 20;

	reg_addr >>= 2;
	if(reg_addr > 0x1f)
		return -1;
        
	p_tbuf = kzalloc(4, GFP_KERNEL);
	if(p_tbuf == NULL)
                return -ENOMEM;

	p_tbuf[0] = 0x80 | (reg_addr & 0x1f);

	ret = esp_common_write_with_addr(epub, SLC_HOST_WIN_CMD, p_tbuf, 4, ESP_SIF_NOSYNC);

	if(ret == 0)
	{
		do{
			if(retry < 20)
				mdelay(10);
			retry --;
			ret = esp_common_read_with_addr(epub, SLC_HOST_STATE_W0, p_tbuf, 4, ESP_SIF_NOSYNC);
		}while(retry >0 && ret != 0);
	}

	if(ret ==0)
		memcpy(value,p_tbuf,4);

	kfree(p_tbuf);
	return ret;
}

int sif_write_reg_window(struct esp_pub *epub, unsigned int reg_addr,u8 *value)
{
        u8 *p_tbuf = NULL;
	int ret = 0;

	reg_addr >>= 2;
	if(reg_addr > 0x1f)
		return -1;
	
	p_tbuf = kzalloc(8, GFP_KERNEL);
	if(p_tbuf == NULL)
                return -ENOMEM;
    	memcpy(p_tbuf,value,4);
    	p_tbuf[4] = 0xc0 |(reg_addr & 0x1f);

    	ret = esp_common_write_with_addr(epub, SLC_HOST_CONF_W5, p_tbuf, 8, ESP_SIF_NOSYNC);

	kfree(p_tbuf);
    	return ret;
}

int sif_ack_target_read_err(struct esp_pub *epub)
{
	u32 value[1];
	int ret;

	ret = sif_read_reg_window(epub, SLC_RX_LINK, (u8 *)value);
	if(ret)
		return ret;
	value[0] |= SLC_RXLINK_START;
	ret = sif_write_reg_window(epub, SLC_RX_LINK, (u8 *)value);
	return ret;
}

int sif_hda_io_enable(struct esp_pub *epub)
{
        u32 *p_tbuf = NULL;
	int ret;
	
	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	if(p_tbuf == NULL)
                return -ENOMEM;

	*p_tbuf = SLC_TXEOF_ENA | (0x4 << SLC_FIFO_MAP_ENA_S) | SLC_TX_DUMMY_MODE | SLC_HDA_MAP_128K | (0xFE << SLC_TX_PUSH_IDLE_NUM_S);
	ret = sif_write_reg_window(epub, SLC_BRIDGE_CONF, (u8 *)p_tbuf);

	if(ret)
		goto _err;
	
	*p_tbuf = 0x30;
	ret = esp_common_writebyte_with_addr((epub), SLC_HOST_CONF_W4 + 1, (u8)*p_tbuf,  ESP_SIF_NOSYNC);

	if(ret)
		goto _err;
	//set w3 0
	*p_tbuf = 0x1;
	ret = esp_common_writebyte_with_addr((epub), SLC_HOST_CONF_W3, (u8)*p_tbuf,  ESP_SIF_NOSYNC);

_err:
	kfree(p_tbuf);
	return ret;
}

typedef enum _SDIO_INTR_MODE {
	SDIO_INTR_IB = 0,
	SDIO_INTR_OOB_TOGGLE,
	SDIO_INTR_OOB_HIGH_LEVEL,
	SDIO_INTR_OOB_LOW_LEVEL,
} SDIO_INTR_MODE;

#define GEN_GPIO_SEL(_gpio_num, _sel_func, _intr_mode, _offset) (((_offset)<< 9 ) |((_intr_mode) << 7)|((_sel_func) << 4)|(_gpio_num))
//bit[3:0] = gpio num, 2
//bit[6:4] = gpio sel func, 0
//bit[8:7] = gpio intr mode, SDIO_INTR_OOB_TOGGLE
//bit[15:9] = register offset, 0x38

u16 gpio_sel_sets[17] = {
	GEN_GPIO_SEL(0, 0, SDIO_INTR_OOB_TOGGLE, 0x34),//GPIO0
	GEN_GPIO_SEL(1, 3, SDIO_INTR_OOB_TOGGLE, 0x18),//U0TXD
	GEN_GPIO_SEL(2, 0, SDIO_INTR_OOB_TOGGLE, 0x38),//GPIO2
	GEN_GPIO_SEL(3, 3, SDIO_INTR_OOB_TOGGLE, 0x14),//U0RXD
	GEN_GPIO_SEL(4, 0, SDIO_INTR_OOB_TOGGLE, 0x3C),//GPIO4
	GEN_GPIO_SEL(5, 0, SDIO_INTR_OOB_TOGGLE, 0x40),//GPIO5
	GEN_GPIO_SEL(6, 3, SDIO_INTR_OOB_TOGGLE, 0x1C),//SD_CLK
	GEN_GPIO_SEL(7, 3, SDIO_INTR_OOB_TOGGLE, 0x20),//SD_DATA0
	GEN_GPIO_SEL(8, 3, SDIO_INTR_OOB_TOGGLE, 0x24),//SD_DATA1
	GEN_GPIO_SEL(9, 3, SDIO_INTR_OOB_TOGGLE, 0x28),//SD_DATA2
	GEN_GPIO_SEL(10, 3, SDIO_INTR_OOB_TOGGLE, 0x2C),//SD_DATA3
	GEN_GPIO_SEL(11, 3, SDIO_INTR_OOB_TOGGLE, 0x30),//SD_CMD
	GEN_GPIO_SEL(12, 3, SDIO_INTR_OOB_TOGGLE, 0x04),//MTDI
	GEN_GPIO_SEL(13, 3, SDIO_INTR_OOB_TOGGLE, 0x08),//MTCK
	GEN_GPIO_SEL(14, 3, SDIO_INTR_OOB_TOGGLE, 0x0C),//MTMS
	GEN_GPIO_SEL(15, 3, SDIO_INTR_OOB_TOGGLE, 0x10),//MTDO
	//pls do not change sel before, if you want to change intr mode,change the one blow
	//GEN_GPIO_SEL(2, 0, SDIO_INTR_OOB_TOGGLE, 0x38)
	GEN_GPIO_SEL(2, 0, SDIO_INTR_OOB_LOW_LEVEL, 0x38)
};
#if defined(USE_EXT_GPIO)
u16 gpio_forbidden = 0;
#endif

int sif_interrupt_target(struct esp_pub *epub, u8 index)
{
	u8 low_byte = BIT(index);
	return esp_common_writebyte_with_addr(epub, SLC_HOST_CONF_W4 + 2, low_byte, ESP_SIF_NOSYNC);
	
}

#ifdef USE_EXT_GPIO
int sif_config_gpio_mode(struct esp_pub *epub, u8 gpio_num, u8 gpio_mode)
{
	u32 *p_tbuf = NULL;
	int err;
    
	if((BIT(gpio_num) & gpio_forbidden) || gpio_num > 15)
		return -EINVAL;    

	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	if(p_tbuf == NULL)
		return -ENOMEM;
	*p_tbuf = (gpio_mode << 16) | gpio_sel_sets[gpio_num];
	err = esp_common_write_with_addr(epub, SLC_HOST_CONF_W1, (u8*)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
	kfree(p_tbuf);
	if (err)
		return err;
	
	return sif_interrupt_target(epub, 4);
}

int sif_set_gpio_output(struct esp_pub *epub, u16 mask, u16 value)
{
	u32 *p_tbuf = NULL;
	int err;
        
	mask &= ~gpio_forbidden;
	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	if(p_tbuf == NULL)
		return -ENOMEM;
	*p_tbuf = (mask << 16) | value;
	err = esp_common_write_with_addr(epub, SLC_HOST_CONF_W2, (u8*)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
	kfree(p_tbuf);
	if (err)
		return err;
	
	return sif_interrupt_target(epub, 5);
}

int sif_get_gpio_intr(struct esp_pub *epub, u16 intr_mask, u16 *value)
{
	u32 *p_tbuf = NULL;
	int err;
        
	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	if(p_tbuf == NULL)
		return -ENOMEM;
	*p_tbuf = 0;
	err = esp_common_read_with_addr(epub, SLC_HOST_CONF_W3, (u8*)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
	if (err){
		kfree(p_tbuf);
		return err;
	}

	*value = *p_tbuf & intr_mask;
	kfree(p_tbuf);
	if(*value == 0)
		return 0;
	return sif_interrupt_target(epub, 6);
}

int sif_get_gpio_input(struct esp_pub *epub, u16 *mask, u16 *value)
{
	u32 *p_tbuf = NULL;
	int err;
        
	err = sif_interrupt_target(epub, 3);
	if (err)
		return err;

	udelay(20);
	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	if(p_tbuf == NULL)
		return -ENOMEM;
	*p_tbuf = 0;
	err = esp_common_read_with_addr(epub, SLC_HOST_CONF_W3, (u8*)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
	if (err){
		kfree(p_tbuf);
		return err;
	}

	*mask = *p_tbuf >> 16;
	*value = *p_tbuf & *mask;
	kfree(p_tbuf);
	
	return 0;
}
#endif

void check_target_id(struct esp_pub *epub)
{
	u32 date;
        int err = 0;
        int i;

	EPUB_CTRL_CHECK(epub, _err);

	sif_lock_bus(epub);

        for(i = 0; i < 4; i++) {
                err = esp_common_readbyte_with_addr(epub, SLC_HOST_DATE + i, (u8 *)&date + i, ESP_SIF_NOSYNC);
                err = esp_common_readbyte_with_addr(epub, SLC_HOST_ID + i, (u8 *)&EPUB_TO_CTRL(epub)->target_id + i, ESP_SIF_NOSYNC);
        }

	sif_unlock_bus(epub);

        esp_dbg(ESP_DBG_LOG, "\n\n \t\t SLC data 0x%08x, ID 0x%08x\n\n", date, EPUB_TO_CTRL(epub)->target_id);

        switch(EPUB_TO_CTRL(epub)->target_id) {
        case 0x100:
                EPUB_TO_CTRL(epub)->slc_window_end_addr = 0x20000;
                break;
        case 0x600:
                EPUB_TO_CTRL(epub)->slc_window_end_addr = 0x20000 - 0x800;

		do{
			u16 gpio_sel;
			u8 low_byte = 0;
			u8 high_byte = 0;
			u8 byte2 = 0;
			u8 byte3 = 0;
#ifdef USE_OOB_INTR 
			gpio_sel = gpio_sel_sets[16];
			low_byte = gpio_sel;
			high_byte = gpio_sel >> 8;
#ifdef USE_EXT_GPIO
			gpio_forbidden |= BIT(gpio_sel & 0xf);
#endif /* USE_EXT_GPIO */
#endif /* USE_OOB_INTR */

			if(sif_get_bt_config() == 1 && sif_get_rst_config() != 1){
				u8 gpio_num = sif_get_wakeup_gpio_config();
				gpio_sel = gpio_sel_sets[gpio_num];
				byte2 = gpio_sel;
				byte3 = gpio_sel >> 8;
#ifdef USE_EXT_GPIO
				gpio_forbidden |= BIT(gpio_num);
#endif
			}
			sif_lock_bus(epub);
			err = esp_common_writebyte_with_addr(epub, SLC_HOST_CONF_W1, low_byte, ESP_SIF_NOSYNC);
			err = esp_common_writebyte_with_addr(epub, SLC_HOST_CONF_W1 + 1, high_byte, ESP_SIF_NOSYNC);
			err = esp_common_writebyte_with_addr(epub, SLC_HOST_CONF_W1 + 2, byte2, ESP_SIF_NOSYNC);
			err = esp_common_writebyte_with_addr(epub, SLC_HOST_CONF_W1 + 3, byte3, ESP_SIF_NOSYNC);
			sif_unlock_bus(epub);
		}while(0);
                break;
        default:
                EPUB_TO_CTRL(epub)->slc_window_end_addr = 0x20000;
                break;
        }
_err:
	return;
}

u32 sif_get_blksz(struct esp_pub *epub)
{
        EPUB_CTRL_CHECK(epub, _err);

        return EPUB_TO_CTRL(epub)->slc_blk_sz;
_err:
	return 512;
}

u32 sif_get_target_id(struct esp_pub *epub)
{
        EPUB_CTRL_CHECK(epub, _err);

        return EPUB_TO_CTRL(epub)->target_id;
_err:
	return 0x600;
}

#ifdef ESP_USE_SDIO
void sif_dsr(struct sdio_func *func)
{
        struct esp_sdio_ctrl *sctrl = sdio_get_drvdata(func);
#else
void sif_dsr(struct spi_device *spi)
{
        struct esp_spi_ctrl *sctrl = spi_get_drvdata(spi);
        u32 buf[1];
#endif
        static int dsr_cnt = 0, real_intr_cnt = 0, bogus_intr_cnt = 0;
        struct slc_host_regs *regs = &(sctrl->slc_regs);
       esp_dbg(ESP_DBG_TRACE, " %s enter %d \n", __func__, dsr_cnt++);

#ifdef ESP_USE_SPI

       if(sctrl->epub->wait_reset == 1)
       {
           mdelay(50);
           return;
       }

       if(sctrl->epub->enable_int  == 1)
       {
           sif_read_reg_window(sctrl->epub, SLC_INT_ENA, (u8 *)buf);
           buf[0] &= ~(SLC_RX_EOF_INT_ENA);
           buf[0] |= SLC_FRHOST_BIT2_INT_ENA;
           sif_write_reg_window(sctrl->epub, SLC_INT_ENA, (u8 *)buf);

           sctrl->epub->enable_int = 0;
        }
#endif
        atomic_set(&sctrl->irq_handling, 1);

#ifdef ESP_USE_SDIO
        sdio_release_host(sctrl->func);
#endif


        sif_lock_bus(sctrl->epub);


        do {
                int ret =0;

		memset(regs, 0x0, sizeof(struct slc_host_regs));

		ret = esp_common_read_with_addr(sctrl->epub, REG_SLC_HOST_BASE + 8, (u8 *)regs, sizeof(struct slc_host_regs), ESP_SIF_NOSYNC);

                if ( (regs->intr_raw & SLC_HOST_RX_ST) && (ret == 0) ) {
                        esp_dbg(ESP_DBG_TRACE, "%s eal intr cnt: %d", __func__, ++real_intr_cnt);
        	
			esp_dsr(sctrl->epub);

                } else {
#ifdef ESP_ACK_INTERRUPT
			sif_platform_ack_interrupt(sctrl->epub);
#endif //ESP_ACK_INTERRUPT
			sif_unlock_bus(sctrl->epub);

                        esp_dbg(ESP_DBG_TRACE, "%s bogus_intr_cnt %d\n", __func__, ++bogus_intr_cnt);
                }

#ifdef SIF_DEBUG_DSR_DUMP_REG
                dump_slc_regs(regs);
#endif /* SIF_DEBUG_DUMP_DSR */

        } while (0);

#ifdef ESP_USE_SDIO
        sdio_claim_host(func);
#endif

        atomic_set(&sctrl->irq_handling, 0);
}


struct slc_host_regs * sif_get_regs(struct esp_pub *epub) 
{
        EPUB_CTRL_CHECK(epub, _err);

        return &EPUB_TO_CTRL(epub)->slc_regs;
_err:
	return NULL;
}

void sif_disable_target_interrupt(struct esp_pub *epub)
{
	EPUB_FUNC_CHECK(epub, _exit);
	sif_lock_bus(epub);
#ifdef HOST_RESET_BUG
	mdelay(10);
#endif
	memset(EPUB_TO_CTRL(epub)->dma_buffer, 0x00, sizeof(u32));
	esp_common_write_with_addr(epub, SLC_HOST_INT_ENA, EPUB_TO_CTRL(epub)->dma_buffer, sizeof(u32), ESP_SIF_NOSYNC);
#ifdef HOST_RESET_BUG
	mdelay(10);
#endif

	sif_unlock_bus(epub);

	mdelay(1);	

	sif_lock_bus(epub);
	sif_interrupt_target(epub, 7);
	sif_unlock_bus(epub);
_exit:
	return;
}

#ifdef SIF_DEBUG_DSR_DUMP_REG
static void dump_slc_regs(struct slc_host_regs *regs) 
{
        esp_dbg(ESP_DBG_TRACE, "\n\n ------- %s --------------\n", __func__);

        esp_dbg(ESP_DBG_TRACE, " \
                        intr_raw 0x%08X \t \n  \
                        state_w0 0x%08X \t state_w1 0x%08X \n  \
                        config_w0 0x%08X \t config_w1 0x%08X \n \
                        intr_status 0x%08X \t config_w2 0x%08X \n \
                        config_w3 0x%08X \t config_w4 0x%08X \n \
                        token_wdata 0x%08X \t intr_clear 0x%08X \n \
                        intr_enable 0x%08X \n\n", regs->intr_raw, \
                        regs->state_w0, regs->state_w1, regs->config_w0, regs->config_w1, \
                        regs->intr_status, \
                        regs->config_w2, regs->config_w3, regs->config_w4, regs->token_wdata, \
                        regs->intr_clear, regs->intr_enable);
}
#endif /* SIF_DEBUG_DSR_DUMP_REG */

static int bt_config = 0;
void sif_record_bt_config(int value)
{
	bt_config = value;
}

int sif_get_bt_config(void)
{
	return bt_config;
}

static int rst_config = 0;
void sif_record_rst_config(int value)
{
        rst_config = value;
}

int sif_get_rst_config(void)
{
        return rst_config;
}

static int ate_test = 0;
void sif_record_ate_config(int value)
{
    ate_test =value;
}

int sif_get_ate_config(void)
{
    return ate_test;
}

static int retry_reset = 0;
void sif_record_retry_config(void)
{
	retry_reset = 1;
}

int sif_get_retry_config(void)
{
	return retry_reset;
}

static int wakeup_gpio = 12;
void sif_record_wakeup_gpio_config(int value)
{
	wakeup_gpio = value;
}

int sif_get_wakeup_gpio_config(void)
{
	return wakeup_gpio;
}

#ifdef ESP_CLASS
static int fcc_mode  = 0;
void sif_record_fccmode(int value)
{
	fcc_mode = value;
}

int sif_get_fccmode(void)
{
	return fcc_mode;
}
#endif

#if (defined(CONFIG_DEBUG_FS) && defined(DEBUGFS_BOOTMODE)) || defined(ESP_CLASS)
static int esp_run = 0;
void sif_record_esp_run(int value)
{
	esp_run = value;
}

int sif_get_esp_run(void)
{
	return esp_run;
}
#endif
