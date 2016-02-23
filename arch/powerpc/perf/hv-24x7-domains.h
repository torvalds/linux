
/*
 * DOMAIN(name, num, index_kind, is_physical)
 *
 * @name:	An all caps token, suitable for use in generating an enum
 *		member and appending to an event name in sysfs.
 *
 * @num:	The number corresponding to the domain as given in
 *		documentation. We assume the catalog domain and the hcall
 *		domain have the same numbering (so far they do), but this
 *		may need to be changed in the future.
 *
 * @index_kind: A stringifiable token describing the meaning of the index
 *		within the given domain. Must fit the parsing rules of the
 *		perf sysfs api.
 *
 * @is_physical: True if the domain is physical, false otherwise (if virtual).
 *
 * Note: The terms PHYS_CHIP, PHYS_CORE, VCPU correspond to physical chip,
 *	 physical core and virtual processor in 24x7 Counters specifications.
 */

DOMAIN(PHYS_CHIP, 0x01, chip, true)
DOMAIN(PHYS_CORE, 0x02, core, true)
DOMAIN(VCPU_HOME_CORE, 0x03, vcpu, false)
DOMAIN(VCPU_HOME_CHIP, 0x04, vcpu, false)
DOMAIN(VCPU_HOME_NODE, 0x05, vcpu, false)
DOMAIN(VCPU_REMOTE_NODE, 0x06, vcpu, false)
