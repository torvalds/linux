Kernel 4.14 for BPI-R2

## Requirements

Need cross compile tools for the armhf architecture and additional bison and flex-package:
```sh
sudo apt-get install gcc-arm-linux-gnueabihf libc6-armhf-cross u-boot-tools bc make gcc libc6-dev libncurses5-dev libssl-dev bison flex
```

## Usage

```sh
  ./build.sh importconfig
  ./build.sh config
  ./build.sh
```

## Branch details

Kernel upstream + BPI-R2
* 4.14-main
* 4.9-main
* 4.16-main (currently not compiling because of wifi driver)

## Kernel version

Kernel breakdown features by version

|          | 4.4 | 4.9 | 4.14 | 4.16|
|----------| --- | --- | --- | --- |
| PCIe     |  Y  |  Y  |  Y  |  Y?  |
| SATA     |  Y  |  Y  |  Y  |  Y?  |
| 2 GMAC   |  Y  |  Y  |  N  |  N  |
| DSA      |  N  |  Y  |  Y  |  Y?  |
| VLAN     |     |     |  Y  |     |
| HW NAT   |     |  Y  |  Y? |     |
| HW QOS   |     |  Y  |  Y? |     |
| Crypto   |  Y  |  Y  |  Y  |  Y?  |
| WIFI     |     |     |  Y  |     |
| BT       |     |     |     |     |
| VIDEO    |  Y  |  N  |  N  |  N  |
| AUDIO    |  Y  |  N  |  N  |  N  |

kernel 4.4: https://github.com/frank-w/BPI-R2-4.4

## Links

* BPI-R2: http://www.banana-pi.org/r2.html
* Kernel: https://www.kernel.org/
* linux-mediatek: https://patchwork.kernel.org/project/linux-mediatek/list/
* kernelci: https://kernelci.org/boot/mt7623n-bananapi-bpi-r2/
* Forum: http://forum.banana-pi.org/c/Banana-Pi-BPI-R2
* Wiki: http://wiki.fw-web.de/doku.php?id=en/bpi-r2/start

License
----

GPL-2.0

**Free Software, Hell Yeah!**
