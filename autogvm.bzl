load(":target_variants.bzl", "la_variants")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":image_opts.bzl", "boot_image_opts")

target_name = "autogvm"

def define_autogvm():
    _autogvm_in_tree_modules = [
        # keep sorted
        "drivers/block/virtio_blk.ko",
        "drivers/bus/mhi/devices/mhi_dev_uci.ko",
        "drivers/bus/mhi/host/mhi.ko",
        "drivers/char/virtio_console.ko",
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/clk/qcom/virtio_clk.ko",
        "drivers/clk/qcom/virtio_clk_direwolf.ko",
        "drivers/clk/qcom/virtio_clk_lemans.ko",
        "drivers/clk/qcom/virtio_clk_sa8195p.ko",
        "drivers/clk/qcom/virtio_clk_sm6150.ko",
        "drivers/clk/qcom/virtio_clk_sm8150.ko",
        "drivers/crypto/qcom-rng.ko",
        "drivers/dma-buf/heaps/qcom_dma_heaps.ko",
        "drivers/extcon/extcon-usb-gpio.ko",
        "drivers/firmware/qcom-scm.ko",
        "drivers/firmware/qcom_scm_hab.ko",
        "drivers/i2c/busses/i2c-msm-geni.ko",
        "drivers/i2c/busses/i2c-virtio.ko",
        "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
        "drivers/iommu/iommu-logger.ko",
        "drivers/iommu/qcom_iommu_debug.ko",
        "drivers/iommu/qcom_iommu_util.ko",
        "drivers/irqchip/msm_show_resume_irq.ko",
        "drivers/irqchip/qcom-pdc.ko",
        "drivers/mailbox/msm_qmp.ko",
        "drivers/mfd/qcom-spmi-pmic.ko",
        "drivers/mmc/host/cqhci.ko",
        "drivers/mmc/host/sdhci-msm.ko",
        "drivers/net/net_failover.ko",
        "drivers/net/virtio_net.ko",
        "drivers/pci/controller/pci-msm-drv.ko",
        "drivers/pinctrl/qcom/pinctrl-direwolf.ko",
        "drivers/pinctrl/qcom/pinctrl-lemans.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/pinctrl/qcom/pinctrl-sdmshrike.ko",
        "drivers/pinctrl/qcom/pinctrl-sm8150.ko",
        "drivers/pinctrl/qcom/pinctrl-spmi-gpio.ko",
        "drivers/pinctrl/qcom/pinctrl-spmi-mpp.ko",
        "drivers/power/reset/msm-vm-poweroff.ko",
        "drivers/power/supply/wallpower_charger.ko",
        "drivers/regulator/debug-regulator.ko",
        "drivers/regulator/stub-regulator.ko",
        "drivers/regulator/virtio_regulator.ko",
        "drivers/remoteproc/rproc_qcom_common.ko",
        "drivers/remoteproc/subsystem_notif_virt.ko",
        "drivers/rtc/rtc-pm8xxx.ko",
        "drivers/soc/qcom/boot_stats.ko",
        "drivers/soc/qcom/hab/msm_hab.ko",
        "drivers/soc/qcom/hgsl/qcom_hgsl.ko",
        "drivers/soc/qcom/mem_buf/mem_buf.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_dev.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_msgq.ko",
        "drivers/soc/qcom/memshare/heap_mem_ext_v01.ko",
        "drivers/soc/qcom/memshare/msm_memshare.ko",
        "drivers/soc/qcom/qcom_soc_wdt.ko",
        "drivers/soc/qcom/qcom_wdt_core.ko",
        "drivers/soc/qcom/qmi_helpers.ko",
        "drivers/soc/qcom/rename_devices.ko",
        "drivers/soc/qcom/rq_stats.ko",
        "drivers/soc/qcom/secure_buffer.ko",
        "drivers/soc/qcom/usb_bam.ko",
        "drivers/spi/spi-msm-geni.ko",
        "drivers/spi/spidev.ko",
        "drivers/spmi/viospmi-pmic-arb.ko",
        "drivers/tty/serial/msm_geni_serial.ko",
        "drivers/uio/msm_sharedmem/msm_sharedmem.ko",
        "drivers/usb/dwc3/dwc3-msm.ko",
        "drivers/usb/gadget/function/usb_f_cdev.ko",
        "drivers/usb/gadget/function/usb_f_diag.ko",
        "drivers/usb/gadget/function/usb_f_qdss.ko",
        "drivers/usb/misc/ehset.ko",
        "drivers/usb/phy/phy-generic.ko",
        "drivers/usb/phy/phy-msm-snps-hs.ko",
        "drivers/usb/phy/phy-msm-ssusb-qmp.ko",
        "drivers/usb/phy/phy-qcom-emu.ko",
        "drivers/virt/gunyah/gh_msgq.ko",
        "drivers/virt/gunyah/gh_rm_drv.ko",
        "drivers/virtio/virtio_input.ko",
        "drivers/virtio/virtio_mmio.ko",
        "kernel/trace/qcom_ipc_logging.ko",
        "net/core/failover.ko",
        "net/mac80211/mac80211.ko",
        "net/qrtr/qrtr.ko",
        "net/qrtr/qrtr-mhi.ko",
        "net/wireless/cfg80211.ko",
    ]

    _autogvm_consolidate_in_tree_modules = _autogvm_in_tree_modules + [
        # keep sorted
        "drivers/misc/lkdtm/lkdtm.ko",
    ]

    for variant in la_variants:
        if variant == "consolidate":
            mod_list = _autogvm_consolidate_in_tree_modules
        else:
            mod_list = _autogvm_in_tree_modules

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                boot_partition_size = 0x4000000,
                earlycon_addr = "pl011,0x1c090000",
                kernel_vendor_cmdline_extras = [
                    # do not sort
                    "console=ttyAMA0",
                    "androidboot.first_stage_console=1",
                    "androidboot.bootdevice=1c170000.virtio_blk",
                    "bootconfig",
                ],
            ),
        )
