#include <linux/device.h>
#include <asm/mce.h>

enum severity_level {
	MCE_NO_SEVERITY,
	MCE_DEFERRED_SEVERITY,
	MCE_UCNA_SEVERITY = MCE_DEFERRED_SEVERITY,
	MCE_KEEP_SEVERITY,
	MCE_SOME_SEVERITY,
	MCE_AO_SEVERITY,
	MCE_UC_SEVERITY,
	MCE_AR_SEVERITY,
	MCE_PANIC_SEVERITY,
};

#define ATTR_LEN		16
#define INITIAL_CHECK_INTERVAL	5 * 60 /* 5 minutes */

/* One object for each MCE bank, shared by all CPUs */
struct mce_bank {
	u64			ctl;			/* subevents to enable */
	unsigned char init;				/* initialise bank? */
	struct device_attribute attr;			/* device attribute */
	char			attrname[ATTR_LEN];	/* attribute name */
};

extern int (*mce_severity)(struct mce *a, int tolerant, char **msg, bool is_excp);
struct dentry *mce_get_debugfs_dir(void);

extern struct mce_bank *mce_banks;
extern mce_banks_t mce_banks_ce_disabled;

#ifdef CONFIG_X86_MCE_INTEL
unsigned long cmci_intel_adjust_timer(unsigned long interval);
bool mce_intel_cmci_poll(void);
void mce_intel_hcpu_update(unsigned long cpu);
void cmci_disable_bank(int bank);
#else
# define cmci_intel_adjust_timer mce_adjust_timer_default
static inline bool mce_intel_cmci_poll(void) { return false; }
static inline void mce_intel_hcpu_update(unsigned long cpu) { }
static inline void cmci_disable_bank(int bank) { }
#endif

void mce_timer_kick(unsigned long interval);

#ifdef CONFIG_ACPI_APEI
int apei_write_mce(struct mce *m);
ssize_t apei_read_mce(struct mce *m, u64 *record_id);
int apei_check_mce(void);
int apei_clear_mce(u64 record_id);
#else
static inline int apei_write_mce(struct mce *m)
{
	return -EINVAL;
}
static inline ssize_t apei_read_mce(struct mce *m, u64 *record_id)
{
	return 0;
}
static inline int apei_check_mce(void)
{
	return 0;
}
static inline int apei_clear_mce(u64 record_id)
{
	return -EINVAL;
}
#endif
