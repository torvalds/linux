#ifndef _SCSI_SAS_INTERNAL_H
#define _SCSI_SAS_INTERNAL_H

#define SAS_HOST_ATTRS		0
#define SAS_PHY_ATTRS		17
#define SAS_PORT_ATTRS		1
#define SAS_RPORT_ATTRS		7
#define SAS_END_DEV_ATTRS	5
#define SAS_EXPANDER_ATTRS	7

struct sas_internal {
	struct scsi_transport_template t;
	struct sas_function_template *f;
	struct sas_domain_function_template *dft;

	struct device_attribute private_host_attrs[SAS_HOST_ATTRS];
	struct device_attribute private_phy_attrs[SAS_PHY_ATTRS];
	struct device_attribute private_port_attrs[SAS_PORT_ATTRS];
	struct device_attribute private_rphy_attrs[SAS_RPORT_ATTRS];
	struct device_attribute private_end_dev_attrs[SAS_END_DEV_ATTRS];
	struct device_attribute private_expander_attrs[SAS_EXPANDER_ATTRS];

	struct transport_container phy_attr_cont;
	struct transport_container port_attr_cont;
	struct transport_container rphy_attr_cont;
	struct transport_container end_dev_attr_cont;
	struct transport_container expander_attr_cont;

	/*
	 * The array of null terminated pointers to attributes
	 * needed by scsi_sysfs.c
	 */
	struct device_attribute *host_attrs[SAS_HOST_ATTRS + 1];
	struct device_attribute *phy_attrs[SAS_PHY_ATTRS + 1];
	struct device_attribute *port_attrs[SAS_PORT_ATTRS + 1];
	struct device_attribute *rphy_attrs[SAS_RPORT_ATTRS + 1];
	struct device_attribute *end_dev_attrs[SAS_END_DEV_ATTRS + 1];
	struct device_attribute *expander_attrs[SAS_EXPANDER_ATTRS + 1];
};
#define to_sas_internal(tmpl)	container_of(tmpl, struct sas_internal, t)

#endif
