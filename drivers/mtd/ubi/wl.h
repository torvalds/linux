/* SPDX-License-Identifier: GPL-2.0 */
#ifndef UBI_WL_H
#define UBI_WL_H
#ifdef CONFIG_MTD_UBI_FASTMAP
static int anchor_pebs_avalible(struct rb_root *root);
static void update_fastmap_work_fn(struct work_struct *wrk);
static struct ubi_wl_entry *find_anchor_wl_entry(struct rb_root *root);
static struct ubi_wl_entry *get_peb_for_wl(struct ubi_device *ubi);
static void ubi_fastmap_close(struct ubi_device *ubi);
static inline void ubi_fastmap_init(struct ubi_device *ubi, int *count)
{
	/* Reserve enough LEBs to store two fastmaps. */
	*count += (ubi->fm_size / ubi->leb_size) * 2;
	INIT_WORK(&ubi->fm_work, update_fastmap_work_fn);
}
static struct ubi_wl_entry *may_reserve_for_fm(struct ubi_device *ubi,
					       struct ubi_wl_entry *e,
					       struct rb_root *root);
#else /* !CONFIG_MTD_UBI_FASTMAP */
static struct ubi_wl_entry *get_peb_for_wl(struct ubi_device *ubi);
static inline void ubi_fastmap_close(struct ubi_device *ubi) { }
static inline void ubi_fastmap_init(struct ubi_device *ubi, int *count) { }
static struct ubi_wl_entry *may_reserve_for_fm(struct ubi_device *ubi,
					       struct ubi_wl_entry *e,
					       struct rb_root *root) {
	return e;
}
#endif /* CONFIG_MTD_UBI_FASTMAP */
#endif /* UBI_WL_H */
