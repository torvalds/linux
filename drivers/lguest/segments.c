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

/*H:600
 * We've almost completed the Host; there's just one file to go!
 *
 * Segments & The Global Descriptor Table
 *
 * (That title sounds like a bad Nerdcore group.  Not to suggest that there are
 * any good Nerdcore groups, but in high school a friend of mine had a band
 * called Joe Fish and the Chips, so there are definitely worse band names).
 *
 * To refresh: the GDT is a table of 8-byte values describing segments.  Once
 * set up, these segments can be loaded into one of the 6 "segment registers".
 *
 * GDT entries are passed around as "struct desc_struct"s, which like IDT
 * entries are split into two 32-bit members, "a" and "b".  One day, someone
 * will clean that up, and be declared a Hero.  (No pressure, I'm just saying).
 *
 * Anyway, the GDT entry contains a base (the start address of the segment), a
 * limit (the size of the segment - 1), and some flags.  Sounds simple, and it
 * would be, except those zany Intel engineers decided that it was too boring
 * to put the base at one end, the limit at the other, and the flags in
 * between.  They decided to shotgun the bits at random throughout the 8 bytes,
 * like so:
 *
 * 0               16                     40       48  52  56     63
 * [ limit part 1 ][     base part 1     ][ flags ][li][fl][base ]
 *                                                  mit ags part 2
 *                                                part 2
 *
 * As a result, this file contains a certain amount of magic numeracy.  Let's
 * begin.
 */

/* There are several entries we don't let the Guest set.  The TSS entry is the
 * "Task State Segment" which controls all kinds of delicate things.  The
 * LGUEST_CS and LGUEST_DS entries are reserved for the Switcher, and the
 * the Guest can't be trusted to deal with double faults. */
static int ignored_gdt(unsigned int num)
{
	return (num == GDT_ENTRY_TSS
		|| num == GDT_ENTRY_LGUEST_CS
		|| num == GDT_ENTRY_LGUEST_DS
		|| num == GDT_ENTRY_DOUBLEFAULT_TSS);
}

/*H:610 Once the GDT has been changed, we fix the new entries up a little.  We
 * don't care if they're invalid: the worst that can happen is a General
 * Protection Fault in the Switcher when it restores a Guest segment register
 * which tries to use that entry.  Then we kill the Guest for causing such a
 * mess: the message will be "unhandled trap 256". */
static void fixup_gdt_table(struct lguest *lg, unsigned start, unsigned end)
{
	unsigned int i;

	for (i = start; i < end; i++) {
		/* We never copy these ones to real GDT, so we don't care what
		 * they say */
		if (ignored_gdt(i))
			continue;

		/* Segment descriptors contain a privilege level: the Guest is
		 * sometimes careless and leaves this as 0, even though it's
		 * running at privilege level 1.  If so, we fix it here. */
		if ((lg->gdt[i].b & 0x00006000) == 0)
			lg->gdt[i].b |= (GUEST_PL << 13);

		/* Each descriptor has an "accessed" bit.  If we don't set it
		 * now, the CPU will try to set it when the Guest first loads
		 * that entry into a segment register.  But the GDT isn't
		 * writable by the Guest, so bad things can happen. */
		lg->gdt[i].b |= 0x00000100;
	}
}

/* This routine is called at boot or modprobe time for each CPU to set up the
 * "constant" GDT entries for Guests running on that CPU. */
void setup_default_gdt_entries(struct lguest_ro_state *state)
{
	struct desc_struct *gdt = state->guest_gdt;
	unsigned long tss = (unsigned long)&state->guest_tss;

	/* The hypervisor segments are full 0-4G segments, privilege level 0 */
	gdt[GDT_ENTRY_LGUEST_CS] = FULL_EXEC_SEGMENT;
	gdt[GDT_ENTRY_LGUEST_DS] = FULL_SEGMENT;

	/* The TSS segment refers to the TSS entry for this CPU, so we cannot
	 * copy it from the Guest.  Forgive the magic flags */
	gdt[GDT_ENTRY_TSS].a = 0x00000067 | (tss << 16);
	gdt[GDT_ENTRY_TSS].b = 0x00008900 | (tss & 0xFF000000)
		| ((tss >> 16) & 0x000000FF);
}

/* This routine is called before the Guest is run for the first time. */
void setup_guest_gdt(struct lguest *lg)
{
	/* Start with full 0-4G segments... */
	lg->gdt[GDT_ENTRY_KERNEL_CS] = FULL_EXEC_SEGMENT;
	lg->gdt[GDT_ENTRY_KERNEL_DS] = FULL_SEGMENT;
	/* ...except the Guest is allowed to use them, so set the privilege
	 * level appropriately in the flags. */
	lg->gdt[GDT_ENTRY_KERNEL_CS].b |= (GUEST_PL << 13);
	lg->gdt[GDT_ENTRY_KERNEL_DS].b |= (GUEST_PL << 13);
}

/* Like the IDT, we never simply use the GDT the Guest gives us.  We set up the
 * GDTs for each CPU, then we copy across the entries each time we want to run
 * a different Guest on that CPU. */

/* A partial GDT load, for the three "thead-local storage" entries.  Otherwise
 * it's just like load_guest_gdt().  So much, in fact, it would probably be
 * neater to have a single hypercall to cover both. */
void copy_gdt_tls(const struct lguest *lg, struct desc_struct *gdt)
{
	unsigned int i;

	for (i = GDT_ENTRY_TLS_MIN; i <= GDT_ENTRY_TLS_MAX; i++)
		gdt[i] = lg->gdt[i];
}

/* This is the full version */
void copy_gdt(const struct lguest *lg, struct desc_struct *gdt)
{
	unsigned int i;

	/* The default entries from setup_default_gdt_entries() are not
	 * replaced.  See ignored_gdt() above. */
	for (i = 0; i < GDT_ENTRIES; i++)
		if (!ignored_gdt(i))
			gdt[i] = lg->gdt[i];
}

/* This is where the Guest asks us to load a new GDT (LHCALL_LOAD_GDT). */
void load_guest_gdt(struct lguest *lg, unsigned long table, u32 num)
{
	/* We assume the Guest has the same number of GDT entries as the
	 * Host, otherwise we'd have to dynamically allocate the Guest GDT. */
	if (num > ARRAY_SIZE(lg->gdt))
		kill_guest(lg, "too many gdt entries %i", num);

	/* We read the whole thing in, then fix it up. */
	lgread(lg, lg->gdt, table, num * sizeof(lg->gdt[0]));
	fixup_gdt_table(lg, 0, ARRAY_SIZE(lg->gdt));
	/* Mark that the GDT changed so the core knows it has to copy it again,
	 * even if the Guest is run on the same CPU. */
	lg->changed |= CHANGED_GDT;
}

void guest_load_tls(struct lguest *lg, unsigned long gtls)
{
	struct desc_struct *tls = &lg->gdt[GDT_ENTRY_TLS_MIN];

	lgread(lg, tls, gtls, sizeof(*tls)*GDT_ENTRY_TLS_ENTRIES);
	fixup_gdt_table(lg, GDT_ENTRY_TLS_MIN, GDT_ENTRY_TLS_MAX+1);
	lg->changed |= CHANGED_GDT_TLS;
}

/*
 * With this, we have finished the Host.
 *
 * Five of the seven parts of our task are complete.  You have made it through
 * the Bit of Despair (I think that's somewhere in the page table code,
 * myself).
 *
 * Next, we examine "make Switcher".  It's short, but intense.
 */
