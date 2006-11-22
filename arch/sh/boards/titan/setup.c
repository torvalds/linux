/*
 *	Setup for Titan
 */

#include <linux/init.h>
#include <asm/irq.h>
#include <asm/titan.h>
#include <asm/io.h>

extern void __init pcibios_init_platform(void);

static struct ipr_data titan_ipr_map[] = {
	{ TITAN_IRQ_WAN,	IRL0_IPR_ADDR,	IRL0_IPR_POS,	IRL0_PRIORITY },
	{ TITAN_IRQ_LAN,	IRL1_IPR_ADDR,	IRL1_IPR_POS,	IRL1_PRIORITY },
	{ TITAN_IRQ_MPCIA,	IRL2_IPR_ADDR,	IRL2_IPR_POS,	IRL2_PRIORITY },
	{ TITAN_IRQ_USB,	IRL3_IPR_ADDR,	IRL3_IPR_POS,	IRL3_PRIORITY },
};

static void __init init_titan_irq(void)
{
	/* enable individual interrupt mode for externals */
	ctrl_outw(ctrl_inw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);

	make_ipr_irq(titan_ipr_map, ARRAY_SIZE(titan_ipr_map));
}

struct sh_machine_vector mv_titan __initmv = {
	.mv_name =	"Titan",

	.mv_inb =	titan_inb,
	.mv_inw =	titan_inw,
	.mv_inl =	titan_inl,
	.mv_outb =	titan_outb,
	.mv_outw =	titan_outw,
	.mv_outl =	titan_outl,

	.mv_inb_p =	titan_inb_p,
	.mv_inw_p =	titan_inw,
	.mv_inl_p =	titan_inl,
	.mv_outb_p =	titan_outb_p,
	.mv_outw_p =	titan_outw,
	.mv_outl_p =	titan_outl,

	.mv_insl =	titan_insl,
	.mv_outsl =	titan_outsl,

	.mv_ioport_map = titan_ioport_map,

	.mv_init_irq =	init_titan_irq,
	.mv_init_pci =	pcibios_init_platform,
};
ALIAS_MV(titan)
