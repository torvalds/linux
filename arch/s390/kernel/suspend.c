/*
 * Suspend support specific for s390.
 *
 * Copyright IBM Corp. 2009
 *
 * Author(s): Hans-Joachim Picht <hans@linux.vnet.ibm.com>
 */

#include <linux/pfn.h>
#include <linux/suspend.h>
#include <linux/mm.h>
#include <asm/sections.h>
#include <asm/ctl_reg.h>

/*
 * The restore of the saved pages in an hibernation image will set
 * the change and referenced bits in the storage key for each page.
 * Overindication of the referenced bits after an hibernation cycle
 * does not cause any harm but the overindication of the change bits
 * would cause trouble.
 * Use the ARCH_SAVE_PAGE_KEYS hooks to save the storage key of each
 * page to the most significant byte of the associated page frame
 * number in the hibernation image.
 */

/*
 * Key storage is allocated as a linked list of pages.
 * The size of the keys array is (PAGE_SIZE - sizeof(long))
 */
struct page_key_data {
	struct page_key_data *next;
	unsigned char data[];
};

#define PAGE_KEY_DATA_SIZE	(PAGE_SIZE - sizeof(struct page_key_data *))

static struct page_key_data *page_key_data;
static struct page_key_data *page_key_rp, *page_key_wp;
static unsigned long page_key_rx, page_key_wx;
unsigned long suspend_zero_pages;

/*
 * For each page in the hibernation image one additional byte is
 * stored in the most significant byte of the page frame number.
 * On suspend no additional memory is required but on resume the
 * keys need to be memorized until the page data has been restored.
 * Only then can the storage keys be set to their old state.
 */
unsigned long page_key_additional_pages(unsigned long pages)
{
	return DIV_ROUND_UP(pages, PAGE_KEY_DATA_SIZE);
}

/*
 * Free page_key_data list of arrays.
 */
void page_key_free(void)
{
	struct page_key_data *pkd;

	while (page_key_data) {
		pkd = page_key_data;
		page_key_data = pkd->next;
		free_page((unsigned long) pkd);
	}
}

/*
 * Allocate page_key_data list of arrays with enough room to store
 * one byte for each page in the hibernation image.
 */
int page_key_alloc(unsigned long pages)
{
	struct page_key_data *pk;
	unsigned long size;

	size = DIV_ROUND_UP(pages, PAGE_KEY_DATA_SIZE);
	while (size--) {
		pk = (struct page_key_data *) get_zeroed_page(GFP_KERNEL);
		if (!pk) {
			page_key_free();
			return -ENOMEM;
		}
		pk->next = page_key_data;
		page_key_data = pk;
	}
	page_key_rp = page_key_wp = page_key_data;
	page_key_rx = page_key_wx = 0;
	return 0;
}

/*
 * Save the storage key into the upper 8 bits of the page frame number.
 */
void page_key_read(unsigned long *pfn)
{
	unsigned long addr;

	addr = (unsigned long) page_address(pfn_to_page(*pfn));
	*(unsigned char *) pfn = (unsigned char) page_get_storage_key(addr);
}

/*
 * Extract the storage key from the upper 8 bits of the page frame number
 * and store it in the page_key_data list of arrays.
 */
void page_key_memorize(unsigned long *pfn)
{
	page_key_wp->data[page_key_wx] = *(unsigned char *) pfn;
	*(unsigned char *) pfn = 0;
	if (++page_key_wx < PAGE_KEY_DATA_SIZE)
		return;
	page_key_wp = page_key_wp->next;
	page_key_wx = 0;
}

/*
 * Get the next key from the page_key_data list of arrays and set the
 * storage key of the page referred by @address. If @address refers to
 * a "safe" page the swsusp_arch_resume code will transfer the storage
 * key from the buffer page to the original page.
 */
void page_key_write(void *address)
{
	page_set_storage_key((unsigned long) address,
			     page_key_rp->data[page_key_rx], 0);
	if (++page_key_rx >= PAGE_KEY_DATA_SIZE)
		return;
	page_key_rp = page_key_rp->next;
	page_key_rx = 0;
}

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = PFN_DOWN(__pa(&__nosave_begin));
	unsigned long nosave_end_pfn = PFN_DOWN(__pa(&__nosave_end));
	unsigned long eshared_pfn = PFN_DOWN(__pa(&_eshared)) - 1;
	unsigned long stext_pfn = PFN_DOWN(__pa(&_stext));

	/* Always save lowcore pages (LC protection might be enabled). */
	if (pfn <= LC_PAGES)
		return 0;
	if (pfn >= nosave_begin_pfn && pfn < nosave_end_pfn)
		return 1;
	/* Skip memory holes and read-only pages (NSS, DCSS, ...). */
	if (pfn >= stext_pfn && pfn <= eshared_pfn)
		return ipl_info.type == IPL_TYPE_NSS ? 1 : 0;
	if (tprot(PFN_PHYS(pfn)))
		return 1;
	return 0;
}

/*
 * PM notifier callback for suspend
 */
static int suspend_pm_cb(struct notifier_block *nb, unsigned long action,
			 void *ptr)
{
	switch (action) {
	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
		suspend_zero_pages = __get_free_pages(GFP_KERNEL, LC_ORDER);
		if (!suspend_zero_pages)
			return NOTIFY_BAD;
		break;
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
		free_pages(suspend_zero_pages, LC_ORDER);
		break;
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static int __init suspend_pm_init(void)
{
	pm_notifier(suspend_pm_cb, 0);
	return 0;
}
arch_initcall(suspend_pm_init);

void save_processor_state(void)
{
	/* swsusp_arch_suspend() actually saves all cpu register contents.
	 * Machine checks must be disabled since swsusp_arch_suspend() stores
	 * register contents to their lowcore save areas. That's the same
	 * place where register contents on machine checks would be saved.
	 * To avoid register corruption disable machine checks.
	 * We must also disable machine checks in the new psw mask for
	 * program checks, since swsusp_arch_suspend() may generate program
	 * checks. Disabling machine checks for all other new psw masks is
	 * just paranoia.
	 */
	local_mcck_disable();
	/* Disable lowcore protection */
	__ctl_clear_bit(0,28);
	S390_lowcore.external_new_psw.mask &= ~PSW_MASK_MCHECK;
	S390_lowcore.svc_new_psw.mask &= ~PSW_MASK_MCHECK;
	S390_lowcore.io_new_psw.mask &= ~PSW_MASK_MCHECK;
	S390_lowcore.program_new_psw.mask &= ~PSW_MASK_MCHECK;
}

void restore_processor_state(void)
{
	S390_lowcore.external_new_psw.mask |= PSW_MASK_MCHECK;
	S390_lowcore.svc_new_psw.mask |= PSW_MASK_MCHECK;
	S390_lowcore.io_new_psw.mask |= PSW_MASK_MCHECK;
	S390_lowcore.program_new_psw.mask |= PSW_MASK_MCHECK;
	/* Enable lowcore protection */
	__ctl_set_bit(0,28);
	local_mcck_enable();
}
