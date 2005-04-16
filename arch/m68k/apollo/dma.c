#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>

#include <asm/setup.h>
#include <asm/bootinfo.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/apollodma.h>
#include <asm/io.h>

/* note only works for 16 Bit 1 page DMA's */

static unsigned short next_free_xlat_entry=0;

unsigned short dma_map_page(unsigned long phys_addr,int count,int type) {

	unsigned long page_aligned_addr=phys_addr & (~((1<<12)-1));
	unsigned short start_map_addr=page_aligned_addr >> 10;
	unsigned short free_xlat_entry, *xlat_map_entry;
	int i;

	free_xlat_entry=next_free_xlat_entry;
	for(i=0,xlat_map_entry=addr_xlat_map+(free_xlat_entry<<2);i<8;i++,xlat_map_entry++) {
#if 0
		printk("phys_addr: %x, page_aligned_addr: %x, start_map_addr: %x\n",phys_addr,page_aligned_addr,start_map_addr+i);
#endif
		out_be16(xlat_map_entry, start_map_addr+i);
	}

	next_free_xlat_entry+=2;
	if(next_free_xlat_entry>125)
		next_free_xlat_entry=0;

#if 0
	printk("next_free_xlat_entry: %d\n",next_free_xlat_entry);
#endif

	return free_xlat_entry<<10;
}

void dma_unmap_page(unsigned short dma_addr) {

	return ;

}

