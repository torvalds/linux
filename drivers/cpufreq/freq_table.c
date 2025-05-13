// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/cpufreq/freq_table.c
 *
 * Copyright (C) 2002 - 2003 Dominik Brodowski
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/module.h>

/*********************************************************************
 *                     FREQUENCY TABLE HELPERS                       *
 *********************************************************************/

static bool policy_has_boost_freq(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *pos, *table = policy->freq_table;

	if (!table)
		return false;

	cpufreq_for_each_valid_entry(pos, table)
		if (pos->flags & CPUFREQ_BOOST_FREQ)
			return true;

	return false;
}

int cpufreq_frequency_table_cpuinfo(struct cpufreq_policy *policy,
				    struct cpufreq_frequency_table *table)
{
	struct cpufreq_frequency_table *pos;
	unsigned int min_freq = ~0;
	unsigned int max_freq = 0;
	unsigned int freq;

	cpufreq_for_each_valid_entry(pos, table) {
		freq = pos->frequency;

		if ((!cpufreq_boost_enabled() || !policy->boost_enabled)
		    && (pos->flags & CPUFREQ_BOOST_FREQ))
			continue;

		pr_debug("table entry %u: %u kHz\n", (int)(pos - table), freq);
		if (freq < min_freq)
			min_freq = freq;
		if (freq > max_freq)
			max_freq = freq;
	}

	policy->min = policy->cpuinfo.min_freq = min_freq;
	policy->max = max_freq;
	/*
	 * If the driver has set its own cpuinfo.max_freq above max_freq, leave
	 * it as is.
	 */
	if (policy->cpuinfo.max_freq < max_freq)
		policy->max = policy->cpuinfo.max_freq = max_freq;

	if (policy->min == ~0)
		return -EINVAL;
	else
		return 0;
}

int cpufreq_frequency_table_verify(struct cpufreq_policy_data *policy,
				   struct cpufreq_frequency_table *table)
{
	struct cpufreq_frequency_table *pos;
	unsigned int freq, prev_smaller = 0;
	bool found = false;

	pr_debug("request for verification of policy (%u - %u kHz) for cpu %u\n",
					policy->min, policy->max, policy->cpu);

	cpufreq_verify_within_cpu_limits(policy);

	cpufreq_for_each_valid_entry(pos, table) {
		freq = pos->frequency;

		if ((freq >= policy->min) && (freq <= policy->max)) {
			found = true;
			break;
		}

		if ((prev_smaller < freq) && (freq <= policy->max))
			prev_smaller = freq;
	}

	if (!found) {
		policy->max = prev_smaller;
		cpufreq_verify_within_cpu_limits(policy);
	}

	pr_debug("verification lead to (%u - %u kHz) for cpu %u\n",
				policy->min, policy->max, policy->cpu);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_verify);

/*
 * Generic routine to verify policy & frequency table, requires driver to set
 * policy->freq_table prior to it.
 */
int cpufreq_generic_frequency_table_verify(struct cpufreq_policy_data *policy)
{
	if (!policy->freq_table)
		return -ENODEV;

	return cpufreq_frequency_table_verify(policy, policy->freq_table);
}
EXPORT_SYMBOL_GPL(cpufreq_generic_frequency_table_verify);

int cpufreq_table_index_unsorted(struct cpufreq_policy *policy,
				 unsigned int target_freq, unsigned int min,
				 unsigned int max, unsigned int relation)
{
	struct cpufreq_frequency_table optimal = {
		.driver_data = ~0,
		.frequency = 0,
	};
	struct cpufreq_frequency_table suboptimal = {
		.driver_data = ~0,
		.frequency = 0,
	};
	struct cpufreq_frequency_table *pos;
	struct cpufreq_frequency_table *table = policy->freq_table;
	unsigned int freq, diff, i = 0;
	int index;

	pr_debug("request for target %u kHz (relation: %u) for cpu %u\n",
					target_freq, relation, policy->cpu);

	switch (relation) {
	case CPUFREQ_RELATION_H:
		suboptimal.frequency = ~0;
		break;
	case CPUFREQ_RELATION_L:
	case CPUFREQ_RELATION_C:
		optimal.frequency = ~0;
		break;
	}

	cpufreq_for_each_valid_entry_idx(pos, table, i) {
		freq = pos->frequency;

		if (freq < min || freq > max)
			continue;
		if (freq == target_freq) {
			optimal.driver_data = i;
			break;
		}
		switch (relation) {
		case CPUFREQ_RELATION_H:
			if (freq < target_freq) {
				if (freq >= optimal.frequency) {
					optimal.frequency = freq;
					optimal.driver_data = i;
				}
			} else {
				if (freq <= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.driver_data = i;
				}
			}
			break;
		case CPUFREQ_RELATION_L:
			if (freq > target_freq) {
				if (freq <= optimal.frequency) {
					optimal.frequency = freq;
					optimal.driver_data = i;
				}
			} else {
				if (freq >= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.driver_data = i;
				}
			}
			break;
		case CPUFREQ_RELATION_C:
			diff = abs(freq - target_freq);
			if (diff < optimal.frequency ||
			    (diff == optimal.frequency &&
			     freq > table[optimal.driver_data].frequency)) {
				optimal.frequency = diff;
				optimal.driver_data = i;
			}
			break;
		}
	}
	if (optimal.driver_data > i) {
		if (suboptimal.driver_data > i) {
			WARN(1, "Invalid frequency table: %u\n", policy->cpu);
			return 0;
		}

		index = suboptimal.driver_data;
	} else
		index = optimal.driver_data;

	pr_debug("target index is %u, freq is:%u kHz\n", index,
		 table[index].frequency);
	return index;
}
EXPORT_SYMBOL_GPL(cpufreq_table_index_unsorted);

int cpufreq_frequency_table_get_index(struct cpufreq_policy *policy,
		unsigned int freq)
{
	struct cpufreq_frequency_table *pos, *table = policy->freq_table;
	int idx;

	if (unlikely(!table)) {
		pr_debug("%s: Unable to find frequency table\n", __func__);
		return -ENOENT;
	}

	cpufreq_for_each_valid_entry_idx(pos, table, idx)
		if (pos->frequency == freq)
			return idx;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_get_index);

/*
 * show_available_freqs - show available frequencies for the specified CPU
 */
static ssize_t show_available_freqs(struct cpufreq_policy *policy, char *buf,
				    bool show_boost)
{
	ssize_t count = 0;
	struct cpufreq_frequency_table *pos, *table = policy->freq_table;

	if (!table)
		return -ENODEV;

	cpufreq_for_each_valid_entry(pos, table) {
		/*
		 * show_boost = true and driver_data = BOOST freq
		 * display BOOST freqs
		 *
		 * show_boost = false and driver_data = BOOST freq
		 * show_boost = true and driver_data != BOOST freq
		 * continue - do not display anything
		 *
		 * show_boost = false and driver_data != BOOST freq
		 * display NON BOOST freqs
		 */
		if (show_boost ^ (pos->flags & CPUFREQ_BOOST_FREQ))
			continue;

		count += sprintf(&buf[count], "%u ", pos->frequency);
	}
	count += sprintf(&buf[count], "\n");

	return count;

}

#define cpufreq_attr_available_freq(_name)	  \
struct freq_attr cpufreq_freq_attr_##_name##_freqs =     \
__ATTR_RO(_name##_frequencies)

/*
 * scaling_available_frequencies_show - show available normal frequencies for
 * the specified CPU
 */
static ssize_t scaling_available_frequencies_show(struct cpufreq_policy *policy,
						  char *buf)
{
	return show_available_freqs(policy, buf, false);
}
cpufreq_attr_available_freq(scaling_available);

/*
 * scaling_boost_frequencies_show - show available boost frequencies for
 * the specified CPU
 */
static ssize_t scaling_boost_frequencies_show(struct cpufreq_policy *policy,
					      char *buf)
{
	return show_available_freqs(policy, buf, true);
}
cpufreq_attr_available_freq(scaling_boost);

static int set_freq_table_sorted(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *pos, *table = policy->freq_table;
	struct cpufreq_frequency_table *prev = NULL;
	int ascending = 0;

	policy->freq_table_sorted = CPUFREQ_TABLE_UNSORTED;

	cpufreq_for_each_valid_entry(pos, table) {
		if (!prev) {
			prev = pos;
			continue;
		}

		if (pos->frequency == prev->frequency) {
			pr_warn("Duplicate freq-table entries: %u\n",
				pos->frequency);
			return -EINVAL;
		}

		/* Frequency increased from prev to pos */
		if (pos->frequency > prev->frequency) {
			/* But frequency was decreasing earlier */
			if (ascending < 0) {
				pr_debug("Freq table is unsorted\n");
				return 0;
			}

			ascending++;
		} else {
			/* Frequency decreased from prev to pos */

			/* But frequency was increasing earlier */
			if (ascending > 0) {
				pr_debug("Freq table is unsorted\n");
				return 0;
			}

			ascending--;
		}

		prev = pos;
	}

	if (ascending > 0)
		policy->freq_table_sorted = CPUFREQ_TABLE_SORTED_ASCENDING;
	else
		policy->freq_table_sorted = CPUFREQ_TABLE_SORTED_DESCENDING;

	pr_debug("Freq table is sorted in %s order\n",
		 ascending > 0 ? "ascending" : "descending");

	return 0;
}

int cpufreq_table_validate_and_sort(struct cpufreq_policy *policy)
{
	int ret;

	if (!policy->freq_table) {
		/* Freq table must be passed by drivers with target_index() */
		if (has_target_index())
			return -EINVAL;

		return 0;
	}

	ret = cpufreq_frequency_table_cpuinfo(policy, policy->freq_table);
	if (ret)
		return ret;

	/* Driver's may have set this field already */
	if (policy_has_boost_freq(policy))
		policy->boost_supported = true;

	return set_freq_table_sorted(policy);
}

MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("CPUfreq frequency table helpers");
