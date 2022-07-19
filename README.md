# Introduction
This repository contains STMicroelectronics MEMS IIO drivers for Linux/Android kernels.

## Source code integration
From your kernel source code directory add the git remote (i.e. stmems_iio_github) for this repository:
```bash
git remote add stmems_iio_github \
               https://github.com/STMicroelectronics/st-mems-android-linux-drivers-iio.git
```

Fetch the just added remote:
```bash
git fetch stmems_iio_github
```

There are now two ways to integrate the drivers code into the kernel target branch:
* merge (**suggested strategy**)
* rebase

### merge
Merge the stmems_iio_github/master with your target kernel source branch (i.e branch linux-5.4.y):

```bash
git merge --no-fork-point \
          linux-5.4.y \
          stmems_iio_github/master
```

### rebase
Rebase the stmems_iio_github/master on top of your target kernel source branch (i.e branch linux-5.4.y):

```bash
git rebase -Xno-renames \
           --no-fork-point \
           linux-5.4.y \
           stmems_iio_github/master
```

## Apply patches
Now that drivers code has been added to the target kernel branch, few patches needs to be added in order to:
* add STM drivers into Kconfig & Makefile systems
* patch IIO framework with custom events with custom events, channels and devices

Apply the patches available in the just added repository (i.e branch linux-5.4.y):

```bash
git am stm_iio_patches/5.4.y/*-stm-*.patch
```

## Configuration
A folder named stm_iio_configs should now be available with the default configs for the supported drivers.

### Modify target defconfig
Sensors defconfig can be appended to the board defconfig (i.e. if your current configuration file is arch/arm/configs/stm32_defconfig):

```bash
cat stm_iio_configs/lsm6dsm_defconfig >> arch/arm/configs/stm32_defconfig
```

Alternatively, it can be done at build time without altering the board config file, as follow.

### Merge configuration
Driver config can be merged into current target pre-configured kernel using a script available in the kernel itself:

```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnu-
scripts/kconfig/merge_config.sh -n .config stm_iio_configs/lsm6dsm_defconfig
```

