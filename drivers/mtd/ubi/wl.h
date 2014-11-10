#ifndef UBI_WL_H
#define UBI_WL_H
#ifdef CONFIG_MTD_UBI_FASTMAP
static int is_fm_block(struct ubi_device *ubi, int pnum);
static int anchor_pebs_avalible(struct rb_root *root);
static void update_fastmap_work_fn(struct work_struct *wrk);
static struct ubi_wl_entry *find_anchor_wl_entry(struct rb_root *root);
static struct ubi_wl_entry *get_peb_for_wl(struct ubi_device *ubi);
static void ubi_fastmap_close(struct ubi_device *ubi);
#else /* !CONFIG_MTD_UBI_FASTMAP */
static struct ubi_wl_entry *get_peb_for_wl(struct ubi_device *ubi);
static inline int is_fm_block(struct ubi_device *ubi, int pnum)
{
	return 0;
}
static inline void ubi_fastmap_close(struct ubi_device *ubi) { }
#endif /* CONFIG_MTD_UBI_FASTMAP */
#endif /* UBI_WL_H */
