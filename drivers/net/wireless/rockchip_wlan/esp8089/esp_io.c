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
		//return sif_spi_read_sync(epub, buf, len, NOT_CHECK_IDLE);
		return sif_spi_read_sync(epub, buf, len, CHECK_IDLE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		return sif_lldesc_read_raw(epub, buf, len, noround);
#endif
#ifdef ESP_USE_SPI
		//return sif_spi_read_nosync(epub, buf, len, NOT_CHECK_IDLE, noround);
		return sif_spi_read_nosync(epub, buf, len, CHECK_IDLE, noround);
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
		//return sif_spi_write_sync(epub, buf, len, NOT_CHECK_IDLE);
		return sif_spi_write_sync(epub, buf, len, CHECK_IDLE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		return sif_lldesc_write_raw(epub, buf, len);
#endif
#ifdef ESP_USE_SPI
		//return sif_spi_write_nosync(epub, buf, len, NOT_CHECK_IDLE);
		return sif_spi_write_nosync(epub, buf, len, CHECK_IDLE);
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
		return sif_spi_epub_read_mix_sync(epub, addr, buf, len, CHECK_IDLE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		return sif_io_raw(epub, addr, buf, len, SIF_FROM_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_read_mix_nosync(epub, addr, buf, len, CHECK_IDLE);
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
		return sif_spi_epub_write_mix_sync(epub, addr, buf, len, CHECK_IDLE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		return sif_io_raw(epub, addr, buf, len, SIF_TO_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_write_mix_nosync(epub, addr, buf, len, CHECK_IDLE);
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
	return sif_spi_epub_read_mix_sync(epub, addr, buf, 1, CHECK_IDLE);
#endif
	} else {
#ifdef ESP_USE_SDIO
	int res;
	*buf = sdio_io_readb(epub, addr, &res);
	return res;
#endif
#ifdef ESP_USE_SPI
	return sif_spi_epub_read_mix_nosync(epub, addr, buf, 1, CHECK_IDLE);
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
		return sif_spi_epub_write_mix_sync(epub, addr, &buf, 1, CHECK_IDLE);
#endif
	} else {
#ifdef ESP_USE_SDIO
		int res;
		sdio_io_writeb(epub, buf, addr, &res);
		return res;
#endif
#ifdef ESP_USE_SPI
		return sif_spi_epub_write_mix_nosync(epub, addr, &buf, 1, CHECK_IDLE);
#endif
	}
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
		return 1;    

	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	ASSERT(p_tbuf != NULL);
	*p_tbuf = (gpio_mode << 16) | gpio_sel_sets[gpio_num];
	err = esp_common_write_with_addr(epub, SLC_HOST_CONF_W1, (u8*)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
	if (err)
		return err;

	kfree(p_tbuf);
	
	return sif_interrupt_target(epub, 4);
}

int sif_set_gpio_output(struct esp_pub *epub, u16 mask, u16 value)
{
	u32 *p_tbuf = NULL;
	int err;
        
	mask &= ~gpio_forbidden;
	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	ASSERT(p_tbuf != NULL);
	*p_tbuf = (mask << 16) | value;
	err = esp_common_write_with_addr(epub, SLC_HOST_CONF_W2, (u8*)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
	if (err)
		return err;

	kfree(p_tbuf);
	
	return sif_interrupt_target(epub, 5);
}

int sif_get_gpio_intr(struct esp_pub *epub, u16 intr_mask, u16 *value)
{
	u32 *p_tbuf = NULL;
	int err;
        
	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	ASSERT(p_tbuf != NULL);
	*p_tbuf = 0;
	err = esp_common_write_with_addr(epub, SLC_HOST_CONF_W3, (u8*)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
	if (err)
		return err;

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
	ASSERT(p_tbuf != NULL);
	*p_tbuf = 0;
	err = esp_common_write_with_addr(epub, SLC_HOST_CONF_W3, (u8*)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
	if (err)
		return err;

	*mask = *p_tbuf >> 16;
	*value = *p_tbuf & *mask;
	kfree(p_tbuf);
	
	return 0;
}
#endif

void sif_raw_dummy_read(struct esp_pub *epub)
{
	int retry = 0;
        u32 *p_tbuf = NULL;
	static u32 read_err_cnt = 0;
	static u32 write_err_cnt = 0;
	static u32 unknow_err_cnt = 0;
	static u32 check_cnt = 0;
	int ext_cnt = 0;      

	if (atomic_read(&epub->ps.state) == ESP_PM_ON) {
		atomic_set(&epub->ps.state, ESP_PM_OFF);
        } else {
		return ;
	}


	p_tbuf = kzalloc(sizeof(u32), GFP_KERNEL);
	ASSERT(p_tbuf != NULL);
	*p_tbuf = 0;
		
	*p_tbuf = 0x010001ff;

	esp_common_write_with_addr(epub, SLC_HOST_CONF_W4, (u8 *)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);

        do {
		*p_tbuf = 0xffffffff;
		udelay(20);
		esp_common_read_with_addr(epub, SLC_HOST_CONF_W4, (u8 *)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);

		if(*p_tbuf == 0x020001ff){
#ifdef ESP_USE_SPI
			if(--ext_cnt >= 0){
				mdelay(10);
				esp_common_write_with_addr(epub, SLC_HOST_CONF_W4, (u8 *)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);
				retry = -1;
				continue;
			}
#endif
			break;
		}
		
		if(*p_tbuf == 0x010001ff){
			if(retry < 5)
				continue;
		}else if(*p_tbuf == 0x000001ff){
			write_err_cnt++;
			ext_cnt = 3;
		}else if(*p_tbuf == 0xffffffff){
			read_err_cnt++;
			write_err_cnt++;
			ext_cnt = 3;
		}else {
			unknow_err_cnt++;
			ext_cnt = 3;
		}

		*p_tbuf = 0x010001ff;
		udelay(20);
		esp_common_write_with_addr(epub, SLC_HOST_CONF_W4, (u8 *)p_tbuf, sizeof(u32), ESP_SIF_NOSYNC);

        } while (retry++ < 1000);
	
	kfree(p_tbuf);

	if(read_err_cnt || write_err_cnt || unknow_err_cnt){
		if((check_cnt & 0xf) == 0)
			//esp_dbg(ESP_DBG_ERROR, "==============sdio err===============\n,read:%u, write:%u, unknow:%u\n", read_err_cnt,write_err_cnt,unknow_err_cnt);
			esp_dbg(ESP_DBG_ERROR, "r%u,w%u,u%u\n", read_err_cnt,write_err_cnt,unknow_err_cnt);
		check_cnt++;
	}

        if (retry > 1) {
                esp_dbg(ESP_DBG_ERROR, "=========%s tried %d times===========\n", __func__, retry - 1);
                //if (retry>=100)
                //        ASSERT(0);
        }
}

void check_target_id(struct esp_pub *epub)
{
	u32 date;
        int err = 0;
        int i;

	EPUB_CTRL_CHECK(epub);

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

	return;
}

u32 sif_get_blksz(struct esp_pub *epub)
{
        EPUB_CTRL_CHECK(epub);

        return EPUB_TO_CTRL(epub)->slc_blk_sz;
}

u32 sif_get_target_id(struct esp_pub *epub)
{
        EPUB_CTRL_CHECK(epub);

        return EPUB_TO_CTRL(epub)->target_id;
}


#ifdef SIF_CHECK_FIRST_INTR
static bool first_intr_checked = false;
#endif //SIF_CHECK_FIRST_INTR

#ifdef ESP_USE_SDIO
void sif_dsr(struct sdio_func *func)
{
        struct esp_sdio_ctrl *sctrl = sdio_get_drvdata(func);
#else
void sif_dsr(struct spi_device *spi)
{
        struct esp_spi_ctrl *sctrl = spi_get_drvdata(spi);
#endif
#ifdef SIF_DSR_WAR
        static int dsr_cnt = 0, real_intr_cnt = 0, bogus_intr_cnt = 0;
        struct slc_host_regs *regs = &(sctrl->slc_regs);
       esp_dbg(ESP_DBG_TRACE, " %s enter %d \n", __func__, dsr_cnt++);
#endif /* SIF_DSR_WAR */

        atomic_set(&sctrl->irq_handling, 1);

#ifdef ESP_USE_SDIO
        sdio_release_host(sctrl->func);
#endif

#ifdef SIF_DSR_WAR
        do {
#ifdef SIF_CHECK_FIRST_INTR 
                if (likely(first_intr_checked)) {
                        esp_dsr(sctrl->epub);
                        break;
                } 
#endif //SIF_CHECK_FIRST_INTR
          
                memset(regs, 0x0, sizeof(struct slc_host_regs));
                esp_common_read_with_addr(sctrl->epub, REG_SLC_HOST_BASE + 8, (u8 *)regs, sizeof(struct slc_host_regs), ESP_SIF_SYNC);

                if (regs->intr_status & SLC_HOST_RX_ST) {
#ifdef SIF_CHECK_FIRST_INTR 
                	first_intr_checked = true;
#endif //SIF_CHECK_FIRST_INTR
                        esp_dbg(ESP_DBG_TRACE, "%s eal intr cnt: %d", __func__, ++real_intr_cnt);
        	
			esp_dsr(sctrl->epub);

                } else {
#ifdef ESP_ACK_INTERRUPT
			sif_lock_bus(sctrl->epub);
			sif_platform_ack_interrupt(sctrl->epub);
			sif_unlock_bus(sctrl->epub);
#endif //ESP_ACK_INTERRUPT

                        esp_dbg(ESP_DBG_TRACE, "%s bogus_intr_cnt %d\n", __func__, ++bogus_intr_cnt);
                }

#ifdef SIF_DEBUG_DSR_DUMP_REG
                dump_slc_regs(regs);
#endif /* SIF_DEBUG_DUMP_DSR */

        } while (0);

#else
       	esp_dsr(sctrl->epub);
#endif /* SIF_DSR_WAR */

#ifdef ESP_USE_SDIO
        sdio_claim_host(func);
#endif

        atomic_set(&sctrl->irq_handling, 0);
}


struct slc_host_regs * sif_get_regs(struct esp_pub *epub) 
{
        EPUB_CTRL_CHECK(epub);

        return &EPUB_TO_CTRL(epub)->slc_regs;
}

void sif_disable_target_interrupt(struct esp_pub *epub)
{
	EPUB_FUNC_CHECK(epub);
	sif_lock_bus(epub);
#ifdef HOST_RESET_BUG
	mdelay(10);
#endif
	sif_raw_dummy_read(epub);
	memset(EPUB_TO_CTRL(epub)->dma_buffer, 0x00, sizeof(u32));
	esp_common_write_with_addr(epub, SLC_HOST_INT_ENA, EPUB_TO_CTRL(epub)->dma_buffer, sizeof(u32), ESP_SIF_NOSYNC);
#ifdef HOST_RESET_BUG
	mdelay(10);
#endif

	sif_unlock_bus(epub);
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


