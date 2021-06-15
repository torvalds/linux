# Toshiba Electronic Devices & Storage Corporation TC956X PCIe Ethernet Host Driver
Release Date: 26 Mar 2021

Release Version: V_01-00

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