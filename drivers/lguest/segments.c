/*P:600 The x86 architecture has segments, which involve a table of descriptors
 * which can be used to do funky things with virtual address interpretation.
 * We originally used to use segments so the Guest couldn't alter the
 * Guest<->Host Switcher, and then we had to trim Guest segments, and restore
 * for userspace per-thread segments, but trim again for on userspace->kernel
 * transitions...  This nightmarish creation was contained within this file,
 * where we knew not to tread without heavy armament and a change of underwear.
 *
 * In these modern times, the segment handling code consists of simple sanity
 * checks, and the worst you'll experience reading this code is butterfly-rash
 * from frolicking through its parklike serenity. :*/
#include "lg.h"

static int desc_ok(const struct desc_struct *gdt)
{
	/* MBZ=0, P=1, DT=1  */
	return ((gdt->b & 0x00209000) == 0x00009000);
}

static int segment_present(const struct desc_struct *gdt)
{
	return gdt->b & 0x8000;
}

static int ignored_gdt(unsigned int num)
{
	return (num == GDT_ENTRY_TSS
		|| num == GDT_ENTRY_LGUEST_CS
		|| num == GDT_ENTRY_LGUEST_DS
		|| num == GDT_ENTRY_DOUBLEFAULT_TSS);
}

/* We don't allow removal of CS, DS or SS; it doesn't make sense. */
static void check_segment_use(struct lguest *lg, unsigned int desc)
{
	if (lg->regs->gs / 8 == desc)
		lg->regs->gs = 0;
	if (lg->regs->fs / 8 == desc)
		lg->regs->fs = 0;
	if (lg->regs->es / 8 == desc)
		lg->regs->es = 0;
	if (lg->regs->ds / 8 == desc
	    || lg->regs->cs / 8 == desc
	    || lg->regs->ss / 8 == desc)
		kill_guest(lg, "Removed live GDT entry %u", desc);
}

static void fixup_gdt_table(struct lguest *lg, unsigned start, unsigned end)
{
	unsigned int i;

	for (i = start; i < end; i++) {
		/* We never copy these ones to real gdt */
		if (ignored_gdt(i))
			continue;

		/* We could fault in switch_to_guest if they are using
		 * a removed segment. */
		if (!segment_present(&lg->gdt[i])) {
			check_segment_use(lg, i);
			continue;
		}

		if (!desc_ok(&lg->gdt[i]))
			kill_guest(lg, "Bad GDT descriptor %i", i);

		/* DPL 0 presumably means "for use by guest". */
		if ((lg->gdt[i].b & 0x00006000) == 0)
			lg->gdt[i].b |= (GUEST_PL << 13);

		/* Set accessed bit, since gdt isn't writable. */
		lg->gdt[i].b |= 0x00000100;
	}
}

void setup_default_gdt_entries(struct lguest_ro_state *state)
{
	struct desc_struct *gdt = state->guest_gdt;
	unsigned long tss = (unsigned long)&state->guest_tss;

	/* Hypervisor segments. */
	gdt[GDT_ENTRY_LGUEST_CS] = FULL_EXEC_SEGMENT;
	gdt[GDT_ENTRY_LGUEST_DS] = FULL_SEGMENT;

	/* This is the one which we *cannot* copy from guest, since tss
	   is depended on this lguest_ro_state, ie. this cpu. */
	gdt[GDT_ENTRY_TSS].a = 0x00000067 | (tss << 16);
	gdt[GDT_ENTRY_TSS].b = 0x00008900 | (tss & 0xFF000000)
		| ((tss >> 16) & 0x000000FF);
}

void setup_guest_gdt(struct lguest *lg)
{
	lg->gdt[GDT_ENTRY_KERNEL_CS] = FULL_EXEC_SEGMENT;
	lg->gdt[GDT_ENTRY_KERNEL_DS] = FULL_SEGMENT;
	lg->gdt[GDT_ENTRY_KERNEL_CS].b |= (GUEST_PL << 13);
	lg->gdt[GDT_ENTRY_KERNEL_DS].b |= (GUEST_PL << 13);
}

/* This is a fast version for the common case where only the three TLS entries
 * have changed. */
void copy_gdt_tls(const struct lguest *lg, struct desc_struct *gdt)
{
	unsigned int i;

	for (i = GDT_ENTRY_TLS_MIN; i <= GDT_ENTRY_TLS_MAX; i++)
		gdt[i] = lg->gdt[i];
}

void copy_gdt(const struct lguest *lg, struct desc_struct *gdt)
{
	unsigned int i;

	for (i = 0; i < GDT_ENTRIES; i++)
		if (!ignored_gdt(i))
			gdt[i] = lg->gdt[i];
}

void load_guest_gdt(struct lguest *lg, unsigned long table, u32 num)
{
	if (num > ARRAY_SIZE(lg->gdt))
		kill_guest(lg, "too many gdt entries %i", num);

	lgread(lg, lg->gdt, table, num * sizeof(lg->gdt[0]));
	fixup_gdt_table(lg, 0, ARRAY_SIZE(lg->gdt));
	lg->changed |= CHANGED_GDT;
}

void guest_load_tls(struct lguest *lg, unsigned long gtls)
{
	struct desc_struct *tls = &lg->gdt[GDT_ENTRY_TLS_MIN];

	lgread(lg, tls, gtls, sizeof(*tls)*GDT_ENTRY_TLS_ENTRIES);
	fixup_gdt_table(lg, GDT_ENTRY_TLS_MIN, GDT_ENTRY_TLS_MAX+1);
	lg->changed |= CHANGED_GDT_TLS;
}
