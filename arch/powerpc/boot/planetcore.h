#ifndef _PPC_BOOT_PLANETCORE_H_
#define _PPC_BOOT_PLANETCORE_H_

#include "types.h"

#define PLANETCORE_KEY_BOARD_TYPE   "BO"
#define PLANETCORE_KEY_BOARD_REV    "BR"
#define PLANETCORE_KEY_MB_RAM       "D1"
#define PLANETCORE_KEY_MAC_ADDR     "EA"
#define PLANETCORE_KEY_FLASH_SPEED  "FS"
#define PLANETCORE_KEY_IP_ADDR      "IP"
#define PLANETCORE_KEY_KB_NVRAM     "NV"
#define PLANETCORE_KEY_PROCESSOR    "PR"
#define PLANETCORE_KEY_PROC_VARIANT "PV"
#define PLANETCORE_KEY_SERIAL_BAUD  "SB"
#define PLANETCORE_KEY_SERIAL_PORT  "SP"
#define PLANETCORE_KEY_SWITCH       "SW"
#define PLANETCORE_KEY_TEMP_OFFSET  "TC"
#define PLANETCORE_KEY_TARGET_IP    "TIP"
#define PLANETCORE_KEY_CRYSTAL_HZ   "XT"

/* Prepare the table for processing, by turning all newlines
 * into NULL bytes.
 */
void planetcore_prepare_table(char *table);

/* Return the value associated with a given key in text,
 * decimal, or hex format.
 *
 * Returns zero/NULL on failure, non-zero on success.
 */
const char *planetcore_get_key(const char *table, const char *key);
int planetcore_get_decimal(const char *table, const char *key, u64 *val);
int planetcore_get_hex(const char *table, const char *key, u64 *val);

/* Updates the device tree local-mac-address properties based
 * on the EA tag.
 */
void planetcore_set_mac_addrs(const char *table);

/* Sets the linux,stdout-path in the /chosen node.  This requires the
 * linux,planetcore-label property in each serial node.
 */
void planetcore_set_stdout_path(const char *table);

/* Sets the current-speed property in the serial node. */
void planetcore_set_serial_speed(const char *table);

#endif
