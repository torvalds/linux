#ifdef CONFIG_ATAGS_PROC
extern void save_atags(struct tag *tags);
#else
static inline void save_atags(struct tag *tags) { }
#endif

void convert_to_tag_list(struct tag *tags);
struct machine_desc *setup_machine_tags(phys_addr_t __atags_pointer, unsigned int machine_nr);
