/**
  * This file contains functions used in USB Boot command
  * and Boot2/FW update
  */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#define DRV_NAME "usb8xxx"

#include "defs.h"
#include "dev.h"
#include "if_usb.h"

/**
 *  @brief This function issues Boot command to the Boot2 code
 *  @param ivalue   1:Boot from FW by USB-Download
 *                  2:Boot from FW in EEPROM
 *  @return 	   	0
 */
int if_usb_issue_boot_command(wlan_private *priv, int ivalue)
{
	struct usb_card_rec	*cardp = priv->card;
	struct bootcmdstr	sbootcmd;
	int i;

	/* Prepare command */
	sbootcmd.u32magicnumber = cpu_to_le32(BOOT_CMD_MAGIC_NUMBER);
	sbootcmd.u8cmd_tag = ivalue;
	for (i=0; i<11; i++)
		sbootcmd.au8dumy[i]=0x00;
	memcpy(cardp->bulk_out_buffer, &sbootcmd, sizeof(struct bootcmdstr));

	/* Issue command */
	usb_tx_block(priv, cardp->bulk_out_buffer, sizeof(struct bootcmdstr));

	return 0;
}
