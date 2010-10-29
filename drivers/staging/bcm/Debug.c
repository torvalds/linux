#include "headers.h"

void bcm_print_buffer(UINT debug_level, const char *function_name,
		      const char *file_name, int line_number,
		      const unsigned char *buffer, int bufferlen,
		      BASE_TYPE base)
{
	int i;
	static const char * const buff_dump_base[] = {
		"DEC", "HEX", "OCT", "BIN"
	};

	if(debug_level < BCM_SCREAM)
		return;

	printk("\n" KERN_DEBUG "%s:%s:%d:Buffer dump of size 0x%x in the %s:\n",
	       file_name, function_name, line_number, bufferlen, buff_dump_base[1]);

	for(i = 0; i < bufferlen;i++) {
		if(i && !(i%16) )
			printk("\n");
		switch(base) {
		case BCM_BASE_TYPE_DEC:
			printk("%03d ", buffer[i]);
			break;
		case BCM_BASE_TYPE_OCT:
			printk("%0x03o ", buffer[i]);
			break;
		case BCM_BASE_TYPE_BIN:
			printk("%02x ", buffer[i]);
			break;
		case BCM_BASE_TYPE_HEX:
		default:
			printk("%02X ", buffer[i]);
			break;
		}
		printk("\n");
	}
}
