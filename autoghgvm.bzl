load(":image_opts.bzl", "boot_image_opts")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":target_variants.bzl", "la_variants")

target_name = "autoghgvm"

def define_autoghgvm():
    _autoghgvm_in_tree_modules = [
        # keep sorted
        "arch/arm64/gunyah/gh_arm_drv.ko",
        "drivers/block/virtio_blk.ko",
        "drivers/bus/mhi/devices/mhi_dev_uci.ko",
        "drivers/bus/mhi/host/mhi.ko",
        "drivers/clk/qcom/clk-dummy.ko",
        "drivers/clk/qcom/clk-qcom.ko",
        "drivers/dma-buf/heaps/qcom_dma_heaps.ko",
        "drivers/firmware/arm_scmi/scmi_perf_domain.ko",
        "drivers/firmware/arm_scmi/scmi_pm_domain.ko",
        "drivers/firmware/qcom-scm.ko",
        "drivers/i2c/busses/i2c-msm-geni.ko",
        "drivers/i2c/busses/i2c-virtio.ko",
        "drivers/iommu/arm/arm-smmu/arm_smmu.ko",
        "drivers/iommu/iommu-logger.ko",
        "drivers/iommu/qcom_iommu_debug.ko",
        "drivers/iommu/qcom_iommu_util.ko",
        "drivers/mailbox/qcom-ipcc.ko",
        "drivers/net/net_failover.ko",
        "drivers/net/virtio_net.ko",
        "drivers/pci/controller/pcie-qcom-ecam.ko",
        "drivers/pinctrl/qcom/pinctrl-lemans.ko",
        "drivers/pinctrl/qcom/pinctrl-monaco_auto.ko",
        "drivers/pinctrl/qcom/pinctrl-msm.ko",
        "drivers/remoteproc/qcom_sysmon.ko",
        "drivers/remoteproc/rproc_qcom_common.ko",
        "drivers/rpmsg/qcom_glink.ko",
        "drivers/rpmsg/qcom_glink_cma.ko",
        "drivers/rpmsg/qcom_glink_smem.ko",
        "drivers/soc/qcom/debug_symbol.ko",
        "drivers/soc/qcom/hab/msm_hab.ko",
        "drivers/soc/qcom/hgsl/qcom_hgsl.ko",
        "drivers/soc/qcom/mem_buf/mem_buf.ko",
        "drivers/soc/qcom/mem_buf/mem_buf_dev.ko",
        "drivers/soc/qcom/minidump.ko",
        "drivers/soc/qcom/qcom_logbuf_boot_log.ko",
        "drivers/soc/qcom/qcom_logbuf_vendor_hooks.ko",
        "drivers/soc/qcom/qcom_wdt_core.ko",
        "drivers/soc/qcom/qmi_helpers.ko",
        "drivers/soc/qcom/rename_devices.ko",
        "drivers/soc/qcom/rq_stats.ko",
        "drivers/soc/qcom/secure_buffer.ko",
        "drivers/soc/qcom/smem.ko",
        "drivers/spi/spi-msm-geni.ko",
        "drivers/spi/spidev.ko",
        "drivers/tty/hvc/hvc_gunyah.ko",
        "drivers/tty/serial/msm_geni_serial.ko",
        "drivers/virt/gunyah/gh_ctrl.ko",
        "drivers/virt/gunyah/gh_dbl.ko",
        "drivers/virt/gunyah/gh_msgq.ko",
        "drivers/virt/gunyah/gh_rm_drv.ko",
        "drivers/virt/gunyah/gh_virt_wdt.ko",
        "drivers/virtio/virtio_input.ko",
        "drivers/virtio/virtio_mmio.ko",
        "kernel/trace/qcom_ipc_logging.ko",
        "net/core/failover.ko",
        "net/qrtr/qrtr.ko",
        "net/qrtr/qrtr-mhi.ko",
        "net/vmw_vsock/vmw_vsock_virtio_transport.ko",
        "net/wireless/cfg80211.ko",
    ]

    _autoghgvm_consolidate_in_tree_modules = _autoghgvm_in_tree_modules + [
        # keep sorted
    ]

    for variant in la_variants:
        if variant == "consolidate":
            mod_list = _autoghgvm_consolidate_in_tree_modules
        else:
            mod_list = _autoghgvm_in_tree_modules

        define_msm_la(
            msm_target = target_name,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                boot_partition_size = 0x4000000,
                #boot_image_header_version = 2,
                #base_address = 0x80000000,
                #page_size = 4096,
                kernel_vendor_cmdline_extras = [
                    # do not sort
                    "console=hvc0",
                    "androidboot.first_stage_console=1",
                    "bootconfig",
                ],
            ),
        )
