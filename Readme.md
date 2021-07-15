# Toshiba Electronic Devices & Storage Corporation TC956X PCIe Ethernet Host Driver
Release Date: 15 Jul 2021

Release Version: V_01-00-02 : Limited-tested version

TC956X PCIe EMAC driver is based on "Fedora 30, kernel-5.4.19".

# Compilation & Run: Need to be root user to execute the following steps.
1.  Execute following commands:

    #make clean

    #make
2.	Load phylink module

	#modprobe phylink
3.  Load the driver

	#insmod tc956x_pcie_eth.ko tc956x_speed=X

	In the module parameter tc956x_speed, X is the desired PCIe Gen speed. X can be 3 or 2 or 1.
	Passing module parameter (tc956x_speed=X) is optional.
	If module parameter is not passed, by default Gen3 speed will be selected by the driver.
4.  Remove the driver

	#rmmod tc956x_pcie_eth

# Release Versions:

## TC956X_Host_Driver_20210326_V_01-00:

1. Initial Version

## TC956X_Host_Driver_20210705_V_01-00-01:

1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
2. XFI interface supported and added module parameters for selection of Port0 and Port1 interface
3. kernel_read API replaced with kernel_read_file_from_path API
4. sprintf, vsprintf APIs replaced with vcnsprintf or vcnsprintf APIs
5. API to print IPA DMA channel statistics supported
6. Correction of print statement about selection of C45 PHY for Port0 interface

## TC956X_Host_Driver_20210705_V_01-00-02:

1. XFI interface supported through compile time macro.
2. Removed module parameters for selection of Port0 and Port1 interface
3. Debugfs support for IPA statistics

# Note:

1. Use below commands to advertise with Autonegotiation ON for speeds 10Gbps, 5Gbps, 2.5Gbps, 1Gbps, 100Mbps and 10Mbps as ethtool speed command does not support.

    ethtool -s <interface> advertise 0x1000 autoneg on --> changes the advertisement to 10Gbps
    
    ethtool -s <interface> advertise 0x1000000000000 autoneg on --> changes the advertisement to 5Gbps

    ethtool -s <interface> advertise 0x800000000000 autoneg on --> changes the advertisement to 2.5Gbps

    ethtool -s <interface> advertise 0x020 autoneg on --> changes the advertisement to 1Gbps

    ethtool -s <interface> advertise 0x008 autoneg on --> changes the advertisement to 100Mbps

    ethtool -s <interface> advertise 0x002 autoneg on --> changes the advertisement 10Mbps

2. To select XFI interface, enable macro "#define TC956X_USXGMII_XFI_MODE" in tc956xmac_inc.h.
   If undefined, USXGMII interface will be selected. By default this macro is defined.

3. Regarding the performance, use the below command to increase the dynamic byte queue limit

    $echo "900000" > /sys/devices/pci0000\:00/0000\:00\:01.0/0000\:01\:00.0/0000\:02\:03.0/0000\:05\:00.0/net/enp5s0f0/queues/tx-0/byte_queue_limits/limit_min
    900000 is the random value chosen. It needs to adjust this value on their system and check
    "0000\:00/0000\:00\:01.0/0000\:01\:00.0/0000\:02\:03.0/0000\:05\:00.0/" value can be obtained from the "lspci -t" command

4. The debug counters to check the interrupt count is available.

    "#ethtool -S <interface>" needs to be executed and sample output is as below
  
       total_interrupts: 120109
       lpi_intr_n: 0
       pmt_intr_n: 0
       event_intr_n: 0
       tx_intr_n: 120000
       rx_intr_n: 51
       xpcs_intr_n: 0
       phy_intr_n: 46
       sw_msi_n: 12

   tx_intr_n = No of. Tx interrupts originating from eMAC
   sw_msi_n = No. of SW MSIs triggered by Systick Handler as part of optimized Tx Timer based on Systick approach.
   So total number of interrupts for Tx = tx_intr_n + sw_msi_n
   Please note that whenever Rx interruts are generated, the Host ISR will process the Tx completed descriptors too.