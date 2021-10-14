# Toshiba Electronic Devices & Storage Corporation TC956X PCIe Ethernet Host Driver
Release Date: 14 Oct 2021

Release Version: V_01-00-16 : Limited-tested version

TC956X PCIe EMAC driver is based on "Fedora 30, kernel-5.4.19".

# Compilation & Run: Need to be root user to execute the following steps.
1.  By default, DMA_OFFLOAD_ENABLE is enabled. Execute following commands:

    #make clean

    #make
2.  If IPA offload is not needed, disable macro DMA_OFFLOAD_ENABLE in common.h. set DMA_OFFLOAD = 0 in Makefile and execute following commands:

    #make clean

    #make

    To compile driver with load firmware header (fw.h) use the below command
    #make TC956X_LOAD_FW_HEADER=1 

    In order to compile the Driver to include the code for applying Gen3 setting, execute Make with below argument
    #make TC956X_PCIE_GEN3_SETTING=1

    Please note, incase both fw.h and Gen3 settings are needed, then both arugments need to be specified.

3.	Load phylink module

	#modprobe phylink
4.  Load the driver

	#insmod tc956x_pcie_eth.ko tc956x_speed=X

	In the module parameter tc956x_speed, X is the desired PCIe Gen speed. X can be 3 or 2 or 1.
	Passing module parameter (tc956x_speed=X) is optional.
	If module parameter is not passed, by default Gen3 speed will be selected by the driver.
5.  Remove the driver

	#rmmod tc956x_pcie_eth

# Note:

1. Use below commands to advertise with Autonegotiation ON for speeds 10Gbps, 5Gbps, 2.5Gbps, 1Gbps, 100Mbps and 10Mbps as ethtool speed command does not support.

    ethtool -s <interface> advertise 0x7000 autoneg on --> changes the advertisement to 10Gbps
    
    ethtool -s <interface> advertise 0x1000000006000 autoneg on --> changes the advertisement to 5Gbps

    ethtool -s <interface> advertise 0x800000006000 autoneg on --> changes the advertisement to 2.5Gbps

    ethtool -s <interface> advertise 0x6020 autoneg on --> changes the advertisement to 1Gbps

    ethtool -s <interface> advertise 0x6008 autoneg on --> changes the advertisement to 100Mbps

    ethtool -s <interface> advertise 0x6002 autoneg on --> changes the advertisement 10Mbps

2. Use the below command to insert the kernel module with specific modes for interfaces:
	
    #insmod tc956x_pcie_eth.ko tc956x_port0_interface=x tc956x_port1_interface=y

       argument info:
	     tc956x_port0_interface: For PORT0 interface mode setting
	     tc956x_port1_interface: For PORT1 interface mode setting
	     x = [0: USXGMII, 1: XFI (default), 2: RGMII (unsupported), 3: SGMII]
	     y = [0: USXGMII (unsupported), 1: XFI (unsupported), 2: RGMII, 3: SGMII(default)]
  
    If invalid and unsupported modes are passed as kernel module parameter, the default interface mode will be selected.

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

5. With V_01-00-07, when IPA API start_channel() is invoked for Rx direction, MAC_Address1_High is updated with 0xBF000000. 
   This register setting is almost similar to promiscuous mode. So please install appropriate FRP instructions.

6. From V_01-00-08 onwards, Port0 ethernet interface will not be created only if there is no ethernet PHY attached to it

7. Enable TC956X_PHY_INTERRUPT_MODE_EMAC0 macro for supporting PORT0 Interrupt mode. Disable the macro if the phy driver supports only polling mode.
   Enable TC956X_PHY_INTERRUPT_MODE_EMAC1 macro for supporting PORT1 Interrupt mode. Disable the macro if the phy driver supports only polling mode.

8. Change below macro values for configuration of Link state L0 and L1 transaction delay.
	/* Link state change delay configuration for Upstream Port */
	#define USP_L0s_ENTRY_DELAY	(0x1FU)
	#define USP_L1_ENTRY_DELAY	(0x3FFU)

	/* Link state change delay configuration for Downstream Port-1 */
	#define DSP1_L0s_ENTRY_DELAY	(0x1FU)
	#define DSP1_L1_ENTRY_DELAY	(0x3FFU)

	/* Link state change delay configuration for Downstream Port-2 */
	#define DSP2_L0s_ENTRY_DELAY	(0x1FU)
	#define DSP2_L1_ENTRY_DELAY	(0x3FFU)

	/* Link state change delay configuration for Virtual Downstream Port */
	#define VDSP_L0s_ENTRY_DELAY	(0x1FU)
	#define VDSP_L1_ENTRY_DELAY	(0x3FFU)

	/* Link state change delay configuration for Internal Endpoint */
	#define EP_L0s_ENTRY_DELAY	(0x1FU)
	#define EP_L1_ENTRY_DELAY	(0x3FFU)

Formula:
	L0 entry delay = XXX_L0s_ENTRY_DELAY * 256 ns
	L1 entry delay = XXX_L1_ENTRY_DELAY * 256 ns
	
	XXX_L0s_ENTRY_DELAY range: 1-31
	XXX_L1_ENTRY_DELAY: 1-1023

9. To check vlan feature status execute:
	ethtool -k <interface> | grep vlan

To enable/disable following vlan features execute:
	(a) rx-vlan-filter:
		ethtool -K <interface> rx-vlan-filter <on|off>
	(b) rx-vlan-offload:
		ethtool -K <interface> rxvlan <on|off>
	(c) tx-vlan-offload:
		ethtool -K <interface> txvlan <on|off>

Use following to configure VLAN:
	(a) modprobe 8021q
	(b) vconfig add <interface> <vlanid>
	(c) vconfig set_flag <interface>.<vlanid> 1 0
	(d) ifconfig <interface>.<vlanid> <ip> netmask 255.255.255.0 broadcast <ip mask> up

Default Configuraton:
	(a) Rx vlan filter is disabled.
	(b) Rx valn offload (vlan stripping) is disabled.
	(c) Tx vlan offload is enabled.

10. Please use the below command to insert the kernel module for passing pause frames to application except pause frames from PHY:

	#insmod tc956x_pcie_eth.ko tc956x_port0_filter_phy_pause_frames=x tc956x_port1_filter_phy_pause_frames=x

	argument info:
		tc956x_port0_filter_phy_pause_frames: For PORT0
		tc956x_port1_filter_phy_pause_frames: For PORT1
		x = [0: DISABLE (default), 1: ENABLE]

	If invalid values are passed as kernel module parameter, the default value will be selected.

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

## TC956X_Host_Driver_20210720_V_01-00-03:

1. Debugfs not supported for IPA statistics
2. Default Port1 interface selected as SGMII

## TC956X_Host_Driver_20210722_V_01-00-04:

1. Module parameters for selection of Port0 and Port1 interface

## TC956X_Host_Driver_20210722_V_01-00-05:

1. Dynamic CM3 TAMAP configuration 

## TC956X_Host_Driver_20210722_V_01-00-06:

1. Add support for contiguous allocation of memory

## TC956X_Host_Driver_20210729_V_01-00-07:

1. Add support to set MAC Address register

## TC956X_Host_Driver_20210806_V_01-00-08:

1. Store and use Port0 pci_dev for all DMA allocation/mapping for IPA path
2. Register Port0 as only PCIe device, in case its PHY is not found

## TC956X_Host_Driver_20210816_V_01-00-09:

1. PHY interrupt mode supported through .config_intr and .ack_interrupt API

## TC956X_Host_Driver_20210824_V_01-00-10:

1. TC956X_PCIE_GEN3_SETTING macro setting supported through makefile. By default Gen3 settings will not be applied by the Driver as TC956X_PCIE_GEN3_SETTING is not defined.
2. TC956X_LOAD_FW_HEADER macro setting supported through makefile. By default, TC956X_LOAD_FW_HEADER macro is disabled. If FIRMWARE_NAME is not specified in Makefile, the default value shall be TC956X_Firmware_PCIeBridge.bin
3. Platform APIs supported.
4. Modified PHY C22/C45 debug message.

## TC956X_Host_Driver_20210902_V_01-00-11:

1. Configuration of Link state L0 and L1 transaction delay for PCIe switch ports & Endpoint. By default maximum values are set for L0s and L1 latencies.

## TC956X_Host_Driver_20210909_V_01-00-12:

1. Reverted changes related to usage of Port-0 pci_dev for all DMA allocation/mapping for IPA path

## TC956X_Host_Driver_20210914_V_01-00-13:

1. Synchronization between ethtool vlan features "rx-vlan-offload", "rx-vlan-filter", "tx-vlan-offload" output and register settings.
2. Added ethtool support to update "rx-vlan-offload", "rx-vlan-filter", and "tx-vlan-offload".
3. Removed IOCTL TC956XMAC_VLAN_STRIP_CONFIG.
4. Removed "Disable VLAN Filter" option in IOCTL TC956XMAC_VLAN_FILTERING.

## TC956X_Host_Driver_20210923_V_01-00-14:

1. Updated RX Queue Threshold limits for Activating and Deactivating Flow control 
2. Filtering All pause frames by default.
3. Capturing RBU status and updating to ethtool statistics for both S/W & IPA DMA channels

## TC956X_Host_Driver_20210929_V_01-00-15:

1. Added check for Device presence before changing PCIe ports speed.

## TC956X_Host_Driver_20211014_V_01-00-16:

1. Configuring pause frame control using kernel module parameter also forwarding only Link partner pause frames to Application and filtering PHY pause frames using FRP.
2. Returning error on disabling Receive Flow Control via ethtool for speed other than 10G in XFI mode.
