
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include "am_regs.h"

/*
#define WRITE_PERI_REG              WRITE_CBUS_REG
#define READ_PERI_REG               READ_CBUS_REG
#define SET_PERI_REG_MASK           SET_CBUS_REG_MASK
#define CLEAR_PERI_REG_MASK         CLEAR_CBUS_REG_MASK
#define PREG_SPI_FLASH_CMD          SPI_FLASH_CMD
#define PREG_SPI_FLASH_ADDR         SPI_FLASH_ADDR
#define PREG_SPI_FLASH_CTRL         SPI_FLASH_CTRL
#define PREG_SPI_FLASH_CTRL1        SPI_FLASH_CTRL1
#define PREG_SPI_FLASH_STATUS       SPI_FLASH_STATUS
#define PREG_SPI_FLASH_C0           SPI_FLASH_C0
*/

/*------------------ register bit definition-------------------------------------------*/
/* SPI_FLASH_CMD */
#define SPI_FLASH_READ    31
#define SPI_FLASH_WREN    30
#define SPI_FLASH_WRDI    29
#define SPI_FLASH_RDID    28
#define SPI_FLASH_RDSR    27
#define SPI_FLASH_WRSR    26
#define SPI_FLASH_PP      25
#define SPI_FLASH_SE      24
#define SPI_FLASH_BE      23
#define SPI_FLASH_CE      22
#define SPI_FLASH_DP      21
#define SPI_FLASH_RES     20
#define SPI_HPM           19
#define SPI_FLASH_USR     18
#define SPI_FLASH_USR_ADDR 15
#define SPI_FLASH_USR_DUMMY 14
#define SPI_FLASH_USR_DIN   13
#define SPI_FLASH_USR_DOUT   12
#define SPI_FLASH_USR_DUMMY_BLEN   10
#define SPI_FLASH_USR_CMD     0

/* SPI_FLASH_ADDR */                            
#define SPI_FLASH_BYTES_LEN 24
#define SPI_FLASH_ADDR_START 0

/* SPI_FLASH_CTRL */
#define SPI_ENABLE_AHB    17
#define SPI_SST_AAI       16
#define SPI_RES_RID       15
#define SPI_FREAD_DUAL    14
#define SPI_READ_READ_EN  13
#define SPI_CLK_DIV0      12
#define SPI_CLKCNT_N      8
#define SPI_CLKCNT_H      4
#define SPI_CLKCNT_L      0
/*------------------ end of register bit definition-------------------------------------------*/

#define AMLOGIC_SPI_MAX_FREQ        25000000
#define SPI_DEV_NAME                "spi_nor"

#define FLASH_PAGESIZE      256

/* Flash opcodes. */
#define OPCODE_WREN     0x06    /* Write enable */
#define OPCODE_RDSR     0x05    /* Read status register */
#define OPCODE_WRSR     0x01    /* Write status register */
#define OPCODE_NORM_READ    0x03    /* Read data bytes (low frequency) */
#define OPCODE_FAST_READ    0x0b    /* Read data bytes (high frequency) */
#define OPCODE_PP       0x02    /* Page program (up to 256 bytes) */
#define OPCODE_SE_4K        0x20    /* Erase 4KiB block */
#define OPCODE_SE_32K       0x52    /* Erase 32KiB block */
#define OPCODE_BE       0xd8    /* Sector erase (usually 64KiB) */
#define OPCODE_RDID     0x9f    /* Read JEDEC ID */

/* Status Register bits. */
#define SR_WIP          1   /* Write in progress */
#define SR_WEL          2   /* Write enable latch */
/* meaning of other SR_* bits may differ between vendors */
#define SR_BP0          4   /* Block protect 0 */
#define SR_BP1          8   /* Block protect 1 */
#define SR_BP2          0x10    /* Block protect 2 */
#define SR_SRWD         0x80    /* SR write protect */

/* Define max times to check status register before we give up. */
#define MAX_READY_WAIT_COUNT    100000
#define CMD_SIZE        4

#ifdef CONFIG_SPI_USE_FAST_READ
#define OPCODE_READ     OPCODE_FAST_READ
#define FAST_READ_DUMMY_BYTE 1
#else
#define OPCODE_READ     OPCODE_NORM_READ
#define FAST_READ_DUMMY_BYTE 0
#endif

#ifdef CONFIG_MTD
#define mtd_has_partitions()    (1)
#else
#define mtd_has_partitions()    (0)
#endif

