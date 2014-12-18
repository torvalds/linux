#ifndef LCD_CONTROLLER_REG_H
#define LCD_CONTROLLER_REG_H
#include <mach/io.h>

#define LCD_REG_BASE_ADDR				IO_VPU_BUS_BASE	//#define IO_VPU_BUS_BASE	(IO_APB_BUS_BASE + 0x100000)
#define LCD_CBUS_BASE_ADDR				IO_CBUS_BASE
#define LCD_DSI_BASE_ADDR				IO_MIPI_DSI_BASE

#define LCD_REG_OFFSET(reg)				(reg << 2)
#define LCD_CBUS_OFFSET(reg)			(reg << 2)
#define LCD_DSI_OFFSET(reg)				(reg << 2)

#define LCD_REG_ADDR(reg)				(LCD_REG_BASE_ADDR + LCD_REG_OFFSET(reg))
#define LCD_CBUS_ADDR(reg)				(LCD_CBUS_BASE_ADDR + LCD_CBUS_OFFSET(reg))
#define LCD_DSI_ADDR(reg)				(LCD_DSI_BASE_ADDR + LCD_DSI_OFFSET(reg))

#define WRITE_LCD_REG(reg, val) 					aml_write_reg32(LCD_REG_ADDR(reg), (val))
#define READ_LCD_REG(reg) 							aml_read_reg32(LCD_REG_ADDR(reg))
#define WRITE_LCD_REG_BITS(reg, val, start, len) 	aml_set_reg32_bits(LCD_REG_ADDR(reg), (val),start,len)
#define CLR_LCD_REG_MASK(reg, mask)   				aml_clr_reg32_mask(LCD_REG_ADDR(reg), (mask))
#define SET_LCD_REG_MASK(reg, mask)     			aml_set_reg32_mask(LCD_REG_ADDR(reg), (mask))

#define WRITE_LCD_CBUS_REG(reg, val) 					aml_write_reg32(LCD_CBUS_ADDR(reg), (val))
#define READ_LCD_CBUS_REG(reg) 							aml_read_reg32(LCD_CBUS_ADDR(reg))
#define WRITE_LCD_CBUS_REG_BITS(reg, val, start, len) 	aml_set_reg32_bits(LCD_CBUS_ADDR(reg), (val),start,len)
#define CLR_LCD_CBUS_REG_MASK(reg, mask)   				aml_clr_reg32_mask(LCD_CBUS_ADDR(reg), (mask))
#define SET_LCD_CBUS_REG_MASK(reg, mask)     			aml_set_reg32_mask(LCD_CBUS_ADDR(reg), (mask))

#define WRITE_DSI_REG(reg, val) *(volatile unsigned *)LCD_DSI_ADDR(reg) = (val)
#define READ_DSI_REG(reg) (*(volatile unsigned *)LCD_DSI_ADDR(reg))
#define WRITE_DSI_REG_BITS(reg, val, start, len) \
	WRITE_DSI_REG(reg, (READ_DSI_REG(reg) & ~(((1L<<(len))-1)<<(start))) | ((unsigned)((val)&((1L<<(len))-1)) << (start)))
#endif
