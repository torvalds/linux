
#include <linux/kernel.h>

/*
 * You may set default region here.
 * 0x10 -- America
 */
int wifi_default_region  = 0x10; // Country / region code

/*
 * When WiFi is IDLE with disconnected state as long as
 * wifi_closed_timeout, we will ask Android to turn off WiFi.
 */
int wifi_turnoff_timeout = 6; //unit is 10 seconds, ex. 6 means 60 seconds

/*
 * Please choose the source for mac address.
 *
 *   For the following modules, an eeprom is embedded in module,
 *   so don't care this variable:
 *     01. Samsung SWL-2480
 *   
 *   For the following modules, you need to set the correct value:
 *     01. Atheros AR6102 / AR6122
 */
#define MAC_ADDR_FROM_SOFTMAC_FILE	0 /* from system/etc/firmware/softmac */
#define MAC_ADDR_FROM_RANDOM		1 /* pseudo-random mac */
#define MAC_ADDR_FROM_EEPROM		2 /* If there is an external eeprom */
#define MAC_ADDR_FROM_EEPROM_FILE	3 /* from system/etc/firmware/calData_ar6102_15dBm.bin */

int wifi_mac_addr_source = MAC_ADDR_FROM_RANDOM;

/*
 * When WiFi is IDLE in 2 minutes, we will put WiFi
 * into deep sleep for power saving.
 */
unsigned long driver_ps_timeout = 2 * 60 * 1000; //2 minutes 

/*
 * User customized MAC address.
 *
 * Return value: 0 -- not customized, 1 -- customized.
 */
#if 1
int wifi_customized_mac_addr(u8 *mac)
{
	return 0;
}
#else
extern int kld_get_wifi_mac(u8 *mac);

int wifi_customized_mac_addr(u8 *mac)
{
	kld_get_wifi_mac(mac);

	printk("We are using customized MAC: %02X:%02X:%02X:%02X:%02x:%02x\n",
	       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	
	return 1;
}
#endif

