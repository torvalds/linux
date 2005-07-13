/*
 * OHCI HCD (Host Controller Driver) for USB.
 * 
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * 
 * This file is licenced under the GPL.
 */

/*-------------------------------------------------------------------------*/

/*
 * There's basically three types of memory:
 *	- data used only by the HCD ... kmalloc is fine
 *	- async and periodic schedules, shared by HC and HCD ... these
 *	  need to use dma_pool or dma_alloc_coherent
 *	- driver buffers, read/written by HC ... the hcd glue or the
 *	  device driver provides us with dma addresses
 *
 * There's also PCI "register" data, which is memory mapped.
 * No memory seen by this driver is pagable.
 */

/*-------------------------------------------------------------------------*/

static void ohci_hcd_init (struct ohci_hcd *ohci)
{
	ohci->next_statechange = jiffies;
	spin_lock_init (&ohci->lock);
	INIT_LIST_HEAD (&ohci->pending);
	INIT_WORK (&ohci->rh_resume, ohci_rh_resume, ohci_to_hcd(ohci));
	ohci->reboot_notifier.notifier_call = ohci_reboot;
}

/*-------------------------------------------------------------------------*/

static int ohci_mem_init (struct ohci_hcd *ohci)
{
	ohci->td_cache = dma_pool_create ("ohci_td",
		ohci_to_hcd(ohci)->self.controller,
		sizeof (struct td),
		32 /* byte alignment */,
		0 /* no page-crossing issues */);
	if (!ohci->td_cache)
		return -ENOMEM;
	ohci->ed_cache = dma_pool_create ("ohci_ed",
		ohci_to_hcd(ohci)->self.controller,
		sizeof (struct ed),
		16 /* byte alignment */,
		0 /* no page-crossing issues */);
	if (!ohci->ed_cache) {
		dma_pool_destroy (ohci->td_cache);
		return -ENOMEM;
	}
	return 0;
}

static void ohci_mem_cleanup (struct ohci_hcd *ohci)
{
	if (ohci->td_cache) {
		dma_pool_destroy (ohci->td_cache);
		ohci->td_cache = NULL;
	}
	if (ohci->ed_cache) {
		dma_pool_destroy (ohci->ed_cache);
		ohci->ed_cache = NULL;
	}
}

/*-------------------------------------------------------------------------*/

/* ohci "done list" processing needs this mapping */
static inline struct td *
dma_to_td (struct ohci_hcd *hc, dma_addr_t td_dma)
{
	struct td *td;

	td_dma &= TD_MASK;
	td = hc->td_hash [TD_HASH_FUNC(td_dma)];
	while (td && td->td_dma != td_dma)
		td = td->td_hash;
	return td;
}

/* TDs ... */
static struct td *
td_alloc (struct ohci_hcd *hc, unsigned mem_flags)
{
	dma_addr_t	dma;
	struct td	*td;

	td = dma_pool_alloc (hc->td_cache, mem_flags, &dma);
	if (td) {
		/* in case hc fetches it, make it look dead */
		memset (td, 0, sizeof *td);
		td->hwNextTD = cpu_to_hc32 (hc, dma);
		td->td_dma = dma;
		/* hashed in td_fill */
	}
	return td;
}

static void
td_free (struct ohci_hcd *hc, struct td *td)
{
	struct td	**prev = &hc->td_hash [TD_HASH_FUNC (td->td_dma)];

	while (*prev && *prev != td)
		prev = &(*prev)->td_hash;
	if (*prev)
		*prev = td->td_hash;
	else if ((td->hwINFO & cpu_to_hc32(hc, TD_DONE)) != 0)
		ohci_dbg (hc, "no hash for td %p\n", td);
	dma_pool_free (hc->td_cache, td, td->td_dma);
}

/*-------------------------------------------------------------------------*/

/* EDs ... */
static struct ed *
ed_alloc (struct ohci_hcd *hc, unsigned mem_flags)
{
	dma_addr_t	dma;
	struct ed	*ed;

	ed = dma_pool_alloc (hc->ed_cache, mem_flags, &dma);
	if (ed) {
		memset (ed, 0, sizeof (*ed));
		INIT_LIST_HEAD (&ed->td_list);
		ed->dma = dma;
	}
	return ed;
}

static void
ed_free (struct ohci_hcd *hc, struct ed *ed)
{
	dma_pool_free (hc->ed_cache, ed, ed->dma);
}

