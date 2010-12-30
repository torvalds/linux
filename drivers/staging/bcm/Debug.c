#include "headers.h"

static UINT current_debug_level=BCM_SCREAM;

int bcm_print_buffer( UINT debug_level, const char *function_name,
				  char *file_name, int line_number, unsigned char *buffer, int bufferlen, enum _BASE_TYPE base)
{
	static const char * const buff_dump_base[] = {
		"DEC", "HEX", "OCT", "BIN"
	};
	if(debug_level>=current_debug_level)
	{
		int i=0;
		printk("\n%s:%s:%d:Buffer dump of size 0x%x in the %s:\n", file_name, function_name, line_number, bufferlen, buff_dump_base[1]);
		for(;i<bufferlen;i++)
		{
			if(i && !(i%16) )
				printk("\n");
			switch(base)
			{
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
		}
		printk("\n");
	}
	return 0;
}


