//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------


#include "ar6000_drv.h"
#include "htc.h"
#include <linux/fs.h>

#include "AR6002/hw2.0/hw/gpio_reg.h"
#include "AR6002/hw2.0/hw/si_reg.h"

//
// defines
//

#define MAX_FILENAME 1023
#define EEPROM_WAIT_LIMIT 16 

#define HOST_INTEREST_ITEM_ADDRESS(item)          \
        (AR6002_HOST_INTEREST_ITEM_ADDRESS(item))

#define EEPROM_SZ 768

/* soft mac */
#define ATH_MAC_LEN                         6
#define ATH_SOFT_MAC_TMP_BUF_LEN            64
unsigned char mac_addr[ATH_MAC_LEN];
unsigned char soft_mac_tmp_buf[ATH_SOFT_MAC_TMP_BUF_LEN];
char *p_mac = NULL;
/* soft mac */

//
// static variables
//

static u8 eeprom_data[EEPROM_SZ];
static u32 sys_sleep_reg;
static struct hif_device *p_bmi_device;

//
// Functions
//

/* soft mac */
static int
wmic_ether_aton(const char *orig, u8 *eth)
{
  const char *bufp;
  int i;

  i = 0;
  for(bufp = orig; *bufp != '\0'; ++bufp) {
    unsigned int val;
	int h, l;

	h = hex_to_bin(*bufp++);

	if (h < 0) {
		printk("%s: MAC value is invalid\n", __FUNCTION__);
		break;
	}

	l = hex_to_bin(*bufp++);
	if (l < 0) {
		printk("%s: MAC value is invalid\n", __FUNCTION__);
		break;
	}

	val = (h << 4) | l;

    eth[i] = (unsigned char) (val & 0377);
    if(++i == ATH_MAC_LEN) {
	    /* That's it.  Any trailing junk? */
	    if (*bufp != '\0') {
		    return 0;
	    }
	    return 1;
    }
    if (*bufp != ':')
	    break;
  }
  return 0;
}

static void
update_mac(unsigned char *eeprom, int size, unsigned char *macaddr)
{
	int i;
	u16 *ptr = (u16 *)(eeprom+4);
	u16 checksum = 0;

	memcpy(eeprom+10,macaddr,6);

	*ptr = 0;
	ptr = (u16 *)eeprom;

	for (i=0; i<size; i+=2) {
		checksum ^= *ptr++;
	}
	checksum = ~checksum;

	ptr = (u16 *)(eeprom+4);
	*ptr = checksum;
	return;
}
/* soft mac */

/* Read a Target register and return its value. */
inline void
BMI_read_reg(u32 address, u32 *pvalue)
{
    BMIReadSOCRegister(p_bmi_device, address, pvalue);
}

/* Write a value to a Target register. */
inline void
BMI_write_reg(u32 address, u32 value)
{
    BMIWriteSOCRegister(p_bmi_device, address, value);
}

/* Read Target memory word and return its value. */
inline void
BMI_read_mem(u32 address, u32 *pvalue)
{
    BMIReadMemory(p_bmi_device, address, (u8*)(pvalue), 4);
}

/* Write a word to a Target memory. */
inline void
BMI_write_mem(u32 address, u8 *p_data, u32 sz)
{
    BMIWriteMemory(p_bmi_device, address, (u8*)(p_data), sz); 
}

/*
 * Enable and configure the Target's Serial Interface
 * so we can access the EEPROM.
 */
static void
enable_SI(struct hif_device *p_device)
{
    u32 regval;

    printk("%s\n", __FUNCTION__);

    p_bmi_device = p_device;

    BMI_read_reg(RTC_BASE_ADDRESS+SYSTEM_SLEEP_OFFSET, &sys_sleep_reg);
    BMI_write_reg(RTC_BASE_ADDRESS+SYSTEM_SLEEP_OFFSET, SYSTEM_SLEEP_DISABLE_SET(1)); //disable system sleep temporarily

    BMI_read_reg(RTC_BASE_ADDRESS+CLOCK_CONTROL_OFFSET, &regval);
    regval &= ~CLOCK_CONTROL_SI0_CLK_MASK;
    BMI_write_reg(RTC_BASE_ADDRESS+CLOCK_CONTROL_OFFSET, regval);

    BMI_read_reg(RTC_BASE_ADDRESS+RESET_CONTROL_OFFSET, &regval);
    regval &= ~RESET_CONTROL_SI0_RST_MASK;
    BMI_write_reg(RTC_BASE_ADDRESS+RESET_CONTROL_OFFSET, regval);


    BMI_read_reg(GPIO_BASE_ADDRESS+GPIO_PIN0_OFFSET, &regval);
    regval &= ~GPIO_PIN0_CONFIG_MASK;
    BMI_write_reg(GPIO_BASE_ADDRESS+GPIO_PIN0_OFFSET, regval);

    BMI_read_reg(GPIO_BASE_ADDRESS+GPIO_PIN1_OFFSET, &regval);
    regval &= ~GPIO_PIN1_CONFIG_MASK;
    BMI_write_reg(GPIO_BASE_ADDRESS+GPIO_PIN1_OFFSET, regval);

    /* SI_CONFIG = 0x500a6; */
    regval =    SI_CONFIG_BIDIR_OD_DATA_SET(1)  |
                SI_CONFIG_I2C_SET(1)            |
                SI_CONFIG_POS_SAMPLE_SET(1)     |
                SI_CONFIG_INACTIVE_CLK_SET(1)   |
                SI_CONFIG_INACTIVE_DATA_SET(1)   |
                SI_CONFIG_DIVIDER_SET(6);
    BMI_write_reg(SI_BASE_ADDRESS+SI_CONFIG_OFFSET, regval);
    
}

static void
disable_SI(void)
{
    u32 regval;
    
    printk("%s\n", __FUNCTION__);

    BMI_write_reg(RTC_BASE_ADDRESS+RESET_CONTROL_OFFSET, RESET_CONTROL_SI0_RST_MASK);
    BMI_read_reg(RTC_BASE_ADDRESS+CLOCK_CONTROL_OFFSET, &regval);
    regval |= CLOCK_CONTROL_SI0_CLK_MASK;
    BMI_write_reg(RTC_BASE_ADDRESS+CLOCK_CONTROL_OFFSET, regval);//Gate SI0 clock
    BMI_write_reg(RTC_BASE_ADDRESS+SYSTEM_SLEEP_OFFSET, sys_sleep_reg); //restore system sleep setting
}

/*
 * Tell the Target to start an 8-byte read from EEPROM,
 * putting the results in Target RX_DATA registers.
 */
static void
request_8byte_read(int offset)
{
    u32 regval;

//    printk("%s: request_8byte_read from offset 0x%x\n", __FUNCTION__, offset);

    
    /* SI_TX_DATA0 = read from offset */
        regval =(0xa1<<16)|
                ((offset & 0xff)<<8)    |
                (0xa0 | ((offset & 0xff00)>>7));
    
        BMI_write_reg(SI_BASE_ADDRESS+SI_TX_DATA0_OFFSET, regval);

        regval = SI_CS_START_SET(1)      |
                SI_CS_RX_CNT_SET(8)     |
                SI_CS_TX_CNT_SET(3);
        BMI_write_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, regval);
}

/*
 * Tell the Target to start a 4-byte write to EEPROM,
 * writing values from Target TX_DATA registers.
 */
static void
request_4byte_write(int offset, u32 data)
{
    u32 regval;

    printk("%s: request_4byte_write (0x%x) to offset 0x%x\n", __FUNCTION__, data, offset);

        /* SI_TX_DATA0 = write data to offset */
        regval =    ((data & 0xffff) <<16)    |
                ((offset & 0xff)<<8)    |
                (0xa0 | ((offset & 0xff00)>>7));
        BMI_write_reg(SI_BASE_ADDRESS+SI_TX_DATA0_OFFSET, regval);

        regval =    data >> 16;
        BMI_write_reg(SI_BASE_ADDRESS+SI_TX_DATA1_OFFSET, regval);

        regval =    SI_CS_START_SET(1)      |
                SI_CS_RX_CNT_SET(0)     |
                SI_CS_TX_CNT_SET(6);
        BMI_write_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, regval);
}

/*
 * Check whether or not an EEPROM request that was started
 * earlier has completed yet.
 */
static bool
request_in_progress(void)
{
    u32 regval;

    /* Wait for DONE_INT in SI_CS */
    BMI_read_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, &regval);

//    printk("%s: request in progress SI_CS=0x%x\n", __FUNCTION__, regval);
    if (regval & SI_CS_DONE_ERR_MASK) {
        printk("%s: EEPROM signaled ERROR (0x%x)\n", __FUNCTION__, regval);
    }

    return (!(regval & SI_CS_DONE_INT_MASK));
}

/*
 * try to detect the type of EEPROM,16bit address or 8bit address
 */

static void eeprom_type_detect(void)
{
    u32 regval;
    u8 i = 0;

    request_8byte_read(0x100);
   /* Wait for DONE_INT in SI_CS */
    do{
        BMI_read_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, &regval);
        if (regval & SI_CS_DONE_ERR_MASK) {
            printk("%s: ERROR : address type was wrongly set\n", __FUNCTION__);     
            break;
        }
        if (i++ == EEPROM_WAIT_LIMIT) {
            printk("%s: EEPROM not responding\n", __FUNCTION__);
        }
    } while(!(regval & SI_CS_DONE_INT_MASK));
}

/*
 * Extract the results of a completed EEPROM Read request
 * and return them to the caller.
 */
inline void
read_8byte_results(u32 *data)
{
    /* Read SI_RX_DATA0 and SI_RX_DATA1 */
    BMI_read_reg(SI_BASE_ADDRESS+SI_RX_DATA0_OFFSET, &data[0]);
    BMI_read_reg(SI_BASE_ADDRESS+SI_RX_DATA1_OFFSET, &data[1]);
}


/*
 * Wait for a previously started command to complete.
 * Timeout if the command is takes "too long".
 */
static void
wait_for_eeprom_completion(void)
{
    int i=0;

    while (request_in_progress()) {
        if (i++ == EEPROM_WAIT_LIMIT) {
            printk("%s: EEPROM not responding\n", __FUNCTION__);
        }
    }
}

/*
 * High-level function which starts an 8-byte read,
 * waits for it to complete, and returns the result.
 */
static void
fetch_8bytes(int offset, u32 *data)
{
    request_8byte_read(offset);
    wait_for_eeprom_completion();
    read_8byte_results(data);

    /* Clear any pending intr */
    BMI_write_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, SI_CS_DONE_INT_MASK);
}

/*
 * High-level function which starts a 4-byte write,
 * and waits for it to complete.
 */
inline void
commit_4bytes(int offset, u32 data)
{
    request_4byte_write(offset, data);
    wait_for_eeprom_completion();
}
