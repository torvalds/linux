#ifndef _IEEE1394_HOTPLUG_H
#define _IEEE1394_HOTPLUG_H

/* Unit spec id and sw version entry for some protocols */
#define AVC_UNIT_SPEC_ID_ENTRY		0x0000A02D
#define AVC_SW_VERSION_ENTRY		0x00010001
#define CAMERA_UNIT_SPEC_ID_ENTRY	0x0000A02D
#define CAMERA_SW_VERSION_ENTRY		0x00000100

/* /include/linux/mod_devicetable.h defines:
 *	IEEE1394_MATCH_VENDOR_ID
 *	IEEE1394_MATCH_MODEL_ID
 *	IEEE1394_MATCH_SPECIFIER_ID
 *	IEEE1394_MATCH_VERSION
 *	struct ieee1394_device_id
 */
#include <linux/mod_devicetable.h>

#endif /* _IEEE1394_HOTPLUG_H */
