#if !defined(MALI_DDK_GATOR_API_VERSION)
	#define MALI_DDK_GATOR_API_VERSION 3
#endif
#if !defined(MALI_TRUE)
	#define MALI_TRUE                ((unsigned int)1)
#endif

#if !defined(MALI_FALSE)
	#define MALI_FALSE               ((unsigned int)0)
#endif

struct mali_dd_hwcnt_info {

	/* Passed from Gator to kbase */
	//u32 in_mali_dd_hwcnt_version;
	unsigned short int bitmask[4];

	/* Passed from kbase to Gator */

	/* ptr to counter dump memory */
	void *kernel_dump_buffer;

	/* size of counter dump memory */
	unsigned int size;

	unsigned int gpu_id;

	unsigned int nr_cores;

	unsigned int nr_core_groups;

	/* The cached present bitmaps - these are the same as the corresponding hardware registers*/
	unsigned long int shader_present_bitmap;
};

struct mali_dd_hwcnt_handles;
extern struct mali_dd_hwcnt_handles* mali_dd_hwcnt_init(struct mali_dd_hwcnt_info *in_out_info);
extern void mali_dd_hwcnt_clear(struct mali_dd_hwcnt_info *in_out_info, struct mali_dd_hwcnt_handles *opaque_handles);
extern unsigned int kbase_dd_instr_hwcnt_dump_complete(struct mali_dd_hwcnt_handles *opaque_handles, unsigned int * const success);
extern unsigned int kbase_dd_instr_hwcnt_dump_irq(struct mali_dd_hwcnt_handles *opaque_handles);
