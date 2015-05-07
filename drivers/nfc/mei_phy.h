#ifndef __LOCAL_MEI_PHY_H_
#define __LOCAL_MEI_PHY_H_

#include <linux/mei_cl_bus.h>
#include <net/nfc/hci.h>
#include <linux/uuid.h>

#define MEI_NFC_UUID __UUID_LE(0x0bb17a78, 0x2a8e, 0x4c50, \
		0x94, 0xd4, 0x50, 0x26, 0x67, 0x23, 0x77, 0x5c)
#define MEI_NFC_HEADER_SIZE 10
#define MEI_NFC_MAX_HCI_PAYLOAD 300

struct nfc_mei_phy {
	struct mei_cl_device *device;
	struct nfc_hci_dev *hdev;

	int powered;

	int hard_fault;		/*
				 * < 0 if hardware error occured
				 * and prevents normal operation.
				 */
};

extern struct nfc_phy_ops mei_phy_ops;

int nfc_mei_phy_enable(void *phy_id);
void nfc_mei_phy_disable(void *phy_id);
void nfc_mei_event_cb(struct mei_cl_device *device, u32 events, void *context);
struct nfc_mei_phy *nfc_mei_phy_alloc(struct mei_cl_device *device);
void nfc_mei_phy_free(struct nfc_mei_phy *phy);

#endif /* __LOCAL_MEI_PHY_H_ */
