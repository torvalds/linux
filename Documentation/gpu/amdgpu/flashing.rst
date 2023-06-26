=======================
 dGPU firmware flashing
=======================

IFWI
----
Flashing the dGPU integrated firmware image (IFWI) is supported by GPUs that
use the PSP to orchestrate the update (Navi3x or newer GPUs).
For supported GPUs, `amdgpu` will export a series of sysfs files that can be
used for the flash process.

The IFWI flash process is:

1. Ensure the IFWI image is intended for the dGPU on the system.
2. "Write" the IFWI image to the sysfs file `psp_vbflash`. This will stage the IFWI in memory.
3. "Read" from the `psp_vbflash` sysfs file to initiate the flash process.
4. Poll the `psp_vbflash_status` sysfs file to determine when the flash process completes.

USB-C PD F/W
------------
On GPUs that support flashing an updated USB-C PD firmware image, the process
is done using the `usbc_pd_fw` sysfs file.

* Reading the file will provide the current firmware version.
* Writing the name of a firmware payload stored in `/lib/firmware/amdgpu` to the sysfs file will initiate the flash process.

The firmware payload stored in `/lib/firmware/amdgpu` can be named any name
as long as it doesn't conflict with other existing binaries that are used by
`amdgpu`.

sysfs files
-----------
.. kernel-doc:: drivers/gpu/drm/amd/amdgpu/amdgpu_psp.c
