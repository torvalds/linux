Broadcom introduced new bus as replacement for older SSB. It is based on AMBA,
however from programming point of view there is nothing AMBA specific we use.

Standard AMBA drivers are platform specific, have hardcoded addresses and use
AMBA standard fields like CID and PID.

In case of Broadcom's cards every device consists of:
1) Broadcom specific AMBA device. It is put on AMBA bus, but can not be treated
   as standard AMBA device. Reading it's CID or PID can cause machine lockup.
2) AMBA standard devices called ports or wrappers. They have CIDs (AMBA_CID)
   and PIDs (0x103BB369), but we do not use that info for anything. One of that
   devices is used for managing Broadcom specific core.

Addresses of AMBA devices are not hardcoded in driver and have to be read from
EPROM.

In this situation we decided to introduce separated bus. It can contain up to
16 devices identified by Broadcom specific fields: manufacturer, id, revision
and class.
