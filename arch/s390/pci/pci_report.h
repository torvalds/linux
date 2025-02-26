/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2024
 *
 * Author(s):
 *   Niklas Schnelle <schnelle@linux.ibm.com>
 *
 */
#ifndef __S390_PCI_REPORT_H
#define __S390_PCI_REPORT_H

struct zpci_dev;

int zpci_report_status(struct zpci_dev *zdev, const char *operation, const char *status);

#endif /* __S390_PCI_REPORT_H */
