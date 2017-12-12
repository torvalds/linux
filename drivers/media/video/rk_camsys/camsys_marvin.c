#include "camsys_marvin.h"
#include "camsys_soc_priv.h"
#include "camsys_gpio.h"

#include <dt-bindings/soc/rockchip-system-status.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <linux/rockchip_ion.h>
#include <linux/file.h>
#include <linux/pm_runtime.h>

#include <linux/dma-iommu.h>
#include <drm/rockchip_drm.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>

static const char miscdev_name[] = CAMSYS_MARVIN_DEVNAME;

static int camsys_mrv_iomux_cb(camsys_extdev_t *extdev, void *ptr)
{
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*state;
	int retval = 0;
	char state_str[64] = {0};
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;
	struct device *dev = &(extdev->pdev->dev);
	camsys_soc_priv_t *soc;

	/* DVP IO Config */

	if (extdev->phy.type == CamSys_Phy_Cif) {
		switch (extdev->phy.info.cif.fmt) {
		case CamSys_Fmt_Raw_8b:
		case CamSys_Fmt_Yuv420_8b:
		case CamSys_Fmt_Yuv422_8b:{
			if (extdev->phy.info.cif.cifio ==
				CamSys_SensorBit0_CifBit0) {
				strcpy(state_str, "isp_dvp8bit0");
			} else if (extdev->phy.info.cif.cifio ==
			CamSys_SensorBit0_CifBit2) {
				strcpy(state_str, "isp_dvp8bit2");
			} else if (extdev->phy.info.cif.cifio ==
			CamSys_SensorBit0_CifBit4) {
				strcpy(state_str, "isp_dvp8bit4");
			} else {
				camsys_err("extdev->phy.info.cif.cifio:0x%x is invalidate!",
					extdev->phy.info.cif.cifio);
				goto fail;
			}

			break;
		}

		case CamSys_Fmt_Raw_10b:{
			strcpy(state_str, "isp_dvp10bit");
			break;
		}

		case CamSys_Fmt_Raw_12b:{
			strcpy(state_str, "isp_dvp12bit");
			break;
		}

		default:{
			camsys_err("extdev->phy.info.cif.fmt: 0x%x is invalidate!",
				extdev->phy.info.cif.fmt);
			goto fail;
		}
		}
	} else {
		if (extdev->dev_cfg & CAMSYS_DEVCFG_FLASHLIGHT) {
			if (extdev->dev_cfg & CAMSYS_DEVCFG_PREFLASHLIGHT) {
				strcpy(state_str, "isp_mipi_fl_prefl");
			} else {
				strcpy(state_str, "isp_mipi_fl");
			}
			{
				/*mux triggerout as gpio*/
				/*get gpio index*/
				int flash_trigger_io;
				enum of_gpio_flags flags;

				flash_trigger_io =
					of_get_named_gpio_flags(
					camsys_dev->pdev->dev.of_node,
					"rockchip,gpios", 0, &flags);
				if (gpio_is_valid(flash_trigger_io)) {
					flash_trigger_io =
						of_get_named_gpio_flags(
						camsys_dev->pdev->dev.of_node,
						"rockchip,gpios", 0, &flags);
					gpio_request(flash_trigger_io,
						"camsys_gpio");
					gpio_direction_output(
						flash_trigger_io,
						(~(extdev->fl.fl.active) &
						0x1));
				}
			}
		} else {
			if (CHIP_TYPE == 3399) {
				strcpy(state_str, "cif_clkout");
			} else {
				strcpy(state_str, "default");
			}
		}
	}

	camsys_trace(1, "marvin pinctrl select: %s", state_str);

	pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pinctrl)) {
		camsys_err("devm_pinctrl_get failed!");
		goto fail;
	}
	state = pinctrl_lookup_state(pinctrl,
							state_str);
	if (IS_ERR(state)) {
		camsys_err("pinctrl_lookup_state failed!");
		goto fail;
	}

	if (!IS_ERR(state)) {
		retval = pinctrl_select_state(pinctrl, state);
		if (retval) {
			camsys_err("pinctrl_select_state failed!");
			goto fail;
		}
	}

	if (camsys_dev->soc) {
		soc = (camsys_soc_priv_t *)camsys_dev->soc;
		if (soc->soc_cfg) {
			(soc->soc_cfg)(camsys_dev, Cif_IoDomain_Cfg,
				(void *)&extdev->dovdd.min_uv);
			(soc->soc_cfg)(camsys_dev, Clk_DriverStrength_Cfg,
				(void *)&extdev->clk.driver_strength);
		} else {
			camsys_err("camsys_dev->soc->soc_cfg is NULL!");
		}
	} else {
		camsys_err("camsys_dev->soc is NULL!");
	}

	return 0;
fail:
	return -1;
}

static int camsys_mrv_flash_trigger_cb(void *ptr, int mode, unsigned int on)
{
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;
	struct device *dev = &(camsys_dev->pdev->dev);
	int flash_trigger_io;
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*state;
	char state_str[63] = {0};
	int retval = 0;
	enum of_gpio_flags flags;
	camsys_extdev_t *extdev = NULL;

	if (!on) {
		strcpy(state_str, "isp_flash_as_gpio");
		pinctrl = devm_pinctrl_get(dev);
		if (IS_ERR(pinctrl)) {
			camsys_err("devm_pinctrl_get failed!");
		}
		state = pinctrl_lookup_state(pinctrl, state_str);
		if (IS_ERR(state)) {
			camsys_err("pinctrl_lookup_state failed!");
		}

		if (!IS_ERR(state)) {
			retval = pinctrl_select_state(pinctrl, state);
			if (retval) {
				camsys_err("pinctrl_select_state failed!");
			}
		}

		/*get gpio index*/
		flash_trigger_io = of_get_named_gpio_flags(
			camsys_dev->pdev->dev.of_node,
				"rockchip,gpios", 0, &flags);
		if (gpio_is_valid(flash_trigger_io)) {
			flash_trigger_io = of_get_named_gpio_flags(
				camsys_dev->pdev->dev.of_node,
				"rockchip,gpios", 0, &flags);
			gpio_request(flash_trigger_io, "camsys_gpio");
			/*get flash io active pol*/
			if (!list_empty(&camsys_dev->extdevs.list)) {
				list_for_each_entry(
					extdev, &camsys_dev->extdevs.list,
					list) {
					if (extdev->dev_cfg &
						CAMSYS_DEVCFG_FLASHLIGHT) {
						gpio_direction_output(
							flash_trigger_io,
							(~(extdev->fl.fl.active)
							& 0x1));
					}
				}
			}
		}
	} else {
		strcpy(state_str, "isp_flash_as_trigger_out");
		pinctrl = devm_pinctrl_get(dev);
		if (IS_ERR(pinctrl)) {
			camsys_err("devm_pinctrl_get failed!");
		}
		state = pinctrl_lookup_state(pinctrl,
								state_str);
		if (IS_ERR(state)) {
			camsys_err("pinctrl_lookup_state failed!");
		}

		if (!IS_ERR(state)) {
			retval = pinctrl_select_state(pinctrl, state);
			if (retval) {
				camsys_err("pinctrl_select_state failed!");
			}

		}
	}
	return retval;
}
static struct device *rockchip_get_sysmmu_device_by_compatible(
const char *compt)
{
	struct device_node *dn = NULL;
	struct platform_device *pd = NULL;
	struct device *ret = NULL;

	dn = of_find_compatible_node(NULL, NULL, compt);
	if (!dn) {
		camsys_err("can't find device node %s \r\n", compt);
		return NULL;
	}

	pd = of_find_device_by_node(dn);
	if (!pd) {
		camsys_err(
			"can't find platform device in device node %s \r\n",
			compt);
		return	NULL;
	}
	ret = &pd->dev;

	return ret;

}
#ifdef CONFIG_IOMMU_API
static inline void platform_set_sysmmu(
struct device *iommu, struct device *dev)
{
	dev->archdata.iommu = iommu;
}
#else
static inline void platform_set_sysmmu(
struct device *iommu, struct device *dev)
{
}
#endif


static int camsys_mrv_iommu_cb(void *ptr, camsys_sysctrl_t *devctl)
{
	struct device *iommu_dev = NULL, *dev = NULL;
	struct file *file = NULL;
	struct ion_client *client = NULL;
	struct ion_handle *handle = NULL;
	camsys_iommu_t *iommu = NULL;
	int ret = 0, iommu_enabled = 0;
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;

	of_property_read_u32(camsys_dev->pdev->dev.of_node,
		"rockchip,isp,iommu-enable", &iommu_enabled);
	if (iommu_enabled != 1) {
		camsys_err("isp iommu have not been enabled!\n");
		ret = -1;
		goto iommu_end;
	}

	if (strstr(camsys_dev->miscdev.name, "camsys_marvin1")) {
		iommu_dev =
			rockchip_get_sysmmu_device_by_compatible
				(ISP1_IOMMU_COMPATIBLE_NAME);
	} else {
		if (CHIP_TYPE == 3399) {
			iommu_dev =
				rockchip_get_sysmmu_device_by_compatible
					(ISP0_IOMMU_COMPATIBLE_NAME);
		} else{
			iommu_dev =
				rockchip_get_sysmmu_device_by_compatible
					(ISP_IOMMU_COMPATIBLE_NAME);
		}
	}

	if (!iommu_dev) {
		camsys_err("get iommu device erro!\n");
		ret = -1;
		goto iommu_end;
	}
	dev = &(camsys_dev->pdev->dev);
	iommu = (camsys_iommu_t *)(devctl->rev);
	file = fget(iommu->client_fd);
	if (!file) {
		camsys_err("get client_fd file erro!\n");
		ret = -1;
		goto iommu_end;
	}

	client = file->private_data;

	if (!client) {
		camsys_err("get ion_client erro!\n");
		ret = -1;
		goto iommu_end;
	}

	fput(file);

	handle = ion_import_dma_buf(client, iommu->map_fd);

	camsys_trace(1, "map fd %d ,client fd %d\n",
		iommu->map_fd, iommu->client_fd);
	if (!handle) {
		camsys_err("get ion_handle erro!\n");
		ret = -1;
		goto iommu_end;
	}
	if (devctl->on) {
		platform_set_sysmmu(iommu_dev, dev);
		ret = rockchip_iovmm_activate(dev);
		ret = ion_map_iommu(dev, client, handle,
			&(iommu->linear_addr), &(iommu->len));
	} else {
		ion_unmap_iommu(dev, client, handle);
		platform_set_sysmmu(iommu_dev, dev);
		rockchip_iovmm_deactivate(dev);
	}
iommu_end:
	return ret;
}

static int camsys_drm_dma_attach_device(camsys_dev_t *camsys_dev)
{
	struct iommu_domain *domain = camsys_dev->domain;
	struct device *dev = &camsys_dev->pdev->dev;
	int ret;

	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	dma_set_max_seg_size(dev, DMA_BIT_MASK(32));
	ret = iommu_attach_device(domain, dev);
	if (ret) {
		dev_err(dev, "Failed to attach iommu device\n");
		return ret;
	}

	if (!common_iommu_setup_dma_ops(dev, 0x10000000, SZ_2G, domain->ops)) {
		dev_err(dev, "Failed to set dma_ops\n");
		iommu_detach_device(domain, dev);
		ret = -ENODEV;
	}

	return ret;
}

static void camsys_drm_dma_detach_device(camsys_dev_t *camsys_dev)
{
	struct iommu_domain *domain = camsys_dev->domain;
	struct device *dev = &camsys_dev->pdev->dev;

	iommu_detach_device(domain, dev);
}

static int camsys_mrv_drm_iommu_cb(void *ptr, camsys_sysctrl_t *devctl)
{
	struct device *dev = NULL;
	camsys_iommu_t *iommu = NULL;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	int index = 0;
	int ret = 0;
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;

	dev = &camsys_dev->pdev->dev;
	iommu = (camsys_iommu_t *)(devctl->rev);
	if (devctl->on) {
		/*ummap mapped fd first*/
		int cur_mapped_cnt = camsys_dev->dma_buf_cnt;

		for (index = 0; index < cur_mapped_cnt; index++) {
			if (camsys_dev->dma_buf[index].fd == iommu->map_fd)
				break;
		}
		if (index != cur_mapped_cnt) {
			attach = camsys_dev->dma_buf[index].attach;
			dma_buf = camsys_dev->dma_buf[index].dma_buf;
			sgt = camsys_dev->dma_buf[index].sgt;
			camsys_trace
			(
			2,
			"exist mapped buf, release it before map: attach %p,"
			"dma_buf %p,sgt %p,fd %d,index %d",
			attach,
			dma_buf,
			sgt,
			iommu->map_fd,
			index);
			dma_buf_unmap_attachment
				(attach,
				sgt,
				DMA_BIDIRECTIONAL);
			dma_buf_detach(dma_buf, attach);
			dma_buf_put(dma_buf);
			if (camsys_dev->dma_buf_cnt == 1)
				camsys_drm_dma_detach_device(camsys_dev);
			camsys_dev->dma_buf_cnt--;
			camsys_dev->dma_buf[index].fd = -1;
		}
		/*get a free slot*/
		for (index = 0; index < CAMSYS_DMA_BUF_MAX_NUM; index++)
			if (camsys_dev->dma_buf[index].fd == -1)
				break;

		if (index == CAMSYS_DMA_BUF_MAX_NUM)
			return -ENOMEM;

		if (camsys_dev->dma_buf_cnt == 0) {
			ret = camsys_drm_dma_attach_device(camsys_dev);
			if (ret)
				return ret;
		}

		dma_buf = dma_buf_get(iommu->map_fd);
		if (IS_ERR(dma_buf))
			return PTR_ERR(dma_buf);
		attach = dma_buf_attach(dma_buf, dev);
		if (IS_ERR(attach)) {
			dma_buf_put(dma_buf);
			return PTR_ERR(attach);
		}
		sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR(sgt)) {
			dma_buf_detach(dma_buf, attach);
			dma_buf_put(dma_buf);
			return PTR_ERR(sgt);
		}
		dma_addr = sg_dma_address(sgt->sgl);
		camsys_dev->dma_buf[index].dma_addr = dma_addr;
		camsys_dev->dma_buf[index].attach	= attach;
		camsys_dev->dma_buf[index].dma_buf = dma_buf;
		camsys_dev->dma_buf[index].sgt = sgt;
		camsys_dev->dma_buf[index].fd = iommu->map_fd;
		iommu->linear_addr = dma_addr;
		iommu->len = sg_dma_len(sgt->sgl);
		camsys_dev->dma_buf_cnt++;
		camsys_trace
			(
			2,
			"dma buf map: dma_addr 0x%lx,attach %p,"
			"dma_buf %p,sgt %p,fd %d,buf_cnt %d",
			(unsigned long)dma_addr,
			attach,
			dma_buf,
			sgt,
			iommu->map_fd,
			camsys_dev->dma_buf_cnt);
	} else {
		if (
			(camsys_dev->dma_buf_cnt == 0) ||
			(index < 0) ||
			(index >= CAMSYS_DMA_BUF_MAX_NUM))
			return -EINVAL;

		for (index = 0; index < camsys_dev->dma_buf_cnt; index++) {
			if (camsys_dev->dma_buf[index].fd == iommu->map_fd)
				break;
		}
		if (index == camsys_dev->dma_buf_cnt) {
			camsys_warn("can't find map fd %d", iommu->map_fd);
			return -EINVAL;
		}
		attach = camsys_dev->dma_buf[index].attach;
		dma_buf = camsys_dev->dma_buf[index].dma_buf;
		sgt = camsys_dev->dma_buf[index].sgt;
		dma_addr = sg_dma_address(sgt->sgl);
		camsys_trace
				(
				2,
				"dma buf unmap: dma_addr 0x%lx,attach %p,"
				"dma_buf %p,sgt %p,index %d",
				(unsigned long)dma_addr,
				attach,
				dma_buf,
				sgt,
				index);
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
		dma_buf_detach(dma_buf, attach);
		dma_buf_put(dma_buf);
		if (camsys_dev->dma_buf_cnt == 1)
			camsys_drm_dma_detach_device(camsys_dev);

		camsys_dev->dma_buf_cnt--;
		camsys_dev->dma_buf[index].fd = -1;
	}

	return ret;
}

static int camsys_mrv_reset_cb(void *ptr, unsigned int on)
{
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;
	camsys_soc_priv_t *soc;

	if (camsys_dev->soc) {
		soc = (camsys_soc_priv_t *)camsys_dev->soc;
		if (soc->soc_cfg) {
			(soc->soc_cfg)
				(camsys_dev, Isp_SoftRst,
				(void *)(unsigned long)on);
		} else {
			camsys_err("camsys_dev->soc->soc_cfg is NULL!");
		}
	} else {
		camsys_err("camsys_dev->soc is NULL!");
	}

	return 0;
}

static int camsys_mrv_clkin_cb(void *ptr, unsigned int on)
{
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;
	camsys_mrv_clk_t *clk = (camsys_mrv_clk_t *)camsys_dev->clk;
	unsigned long isp_clk;

	if (CHIP_TYPE == 3399) {
		if (on && !clk->in_on) {
			rockchip_set_system_status(SYS_STATUS_ISP);
			if (on == 1)
				isp_clk = 210000000;
			else
				isp_clk = 420000000;

			if (strstr(camsys_dev->miscdev.name,
				"camsys_marvin1")) {
				clk_set_rate(clk->clk_isp1, isp_clk);
				clk_prepare_enable(clk->hclk_isp1_noc);
				clk_prepare_enable(clk->hclk_isp1_wrapper);
				clk_prepare_enable(clk->aclk_isp1_noc);
				clk_prepare_enable(clk->aclk_isp1_wrapper);
				clk_prepare_enable(clk->clk_isp1);

				clk_prepare_enable(clk->cif_clk_out);
				clk_prepare_enable(clk->pclk_dphy_ref);
				clk_prepare_enable(clk->pclk_dphytxrx);

				clk_prepare_enable(clk->pclkin_isp);
				clk_prepare_enable(clk->cif_clk_out);
			} else {
				clk_set_rate(clk->clk_isp0, isp_clk);
				clk_prepare_enable(clk->hclk_isp0_noc);
				clk_prepare_enable(clk->hclk_isp0_wrapper);
				clk_prepare_enable(clk->aclk_isp0_noc);
				clk_prepare_enable(clk->aclk_isp0_wrapper);
				clk_prepare_enable(clk->clk_isp0);
				clk_prepare_enable(clk->cif_clk_out);
				clk_prepare_enable(clk->pclk_dphyrx);
				clk_prepare_enable(clk->pclk_dphy_ref);
			}

			clk->in_on = true;
			pm_runtime_get_sync(&camsys_dev->pdev->dev);
			camsys_trace(1, "%s clock(f: %ld Hz) in turn on",
				     dev_name(camsys_dev->miscdev.this_device),
				     isp_clk);
			camsys_mrv_reset_cb(ptr, 1);
			udelay(100);
			camsys_mrv_reset_cb(ptr, 0);
		} else if (!on && clk->in_on) {
			pm_runtime_put_sync(&camsys_dev->pdev->dev);
			if (strstr(camsys_dev->miscdev.name,
				"camsys_marvin1")) {
				clk_disable_unprepare(clk->hclk_isp1_noc);
				clk_disable_unprepare(clk->hclk_isp1_wrapper);
				clk_disable_unprepare(clk->aclk_isp1_noc);
				clk_disable_unprepare(clk->aclk_isp1_wrapper);
				clk_disable_unprepare(clk->clk_isp1);

				clk_disable_unprepare(clk->cif_clk_out);
				clk_disable_unprepare(clk->pclk_dphytxrx);
				clk_disable_unprepare(clk->pclk_dphy_ref);

				clk_disable_unprepare(clk->pclkin_isp);
			} else {
				clk_disable_unprepare(clk->hclk_isp0_noc);
				clk_disable_unprepare(clk->hclk_isp0_wrapper);
				clk_disable_unprepare(clk->aclk_isp0_noc);
				clk_disable_unprepare(clk->aclk_isp0_wrapper);
				clk_disable_unprepare(clk->clk_isp0);

				clk_disable_unprepare(clk->cif_clk_out);
				clk_disable_unprepare(clk->pclk_dphyrx);
				clk_disable_unprepare(clk->pclk_dphy_ref);
			}

			rockchip_clear_system_status(SYS_STATUS_ISP);
			clk->in_on = false;
			camsys_trace(1, "%s clock in turn off",
				     dev_name(camsys_dev->miscdev.this_device));
			}
	} else{
		if (on && !clk->in_on) {
			rockchip_set_system_status(SYS_STATUS_ISP);

		if (on == 1)
			isp_clk = 210000000;
		else
			isp_clk = 420000000;

		clk_set_rate(clk->isp, isp_clk);
		clk_set_rate(clk->isp_jpe, isp_clk);

		/* clk_prepare_enable(clk->pd_isp); */
		clk_prepare_enable(clk->aclk_isp);
		clk_prepare_enable(clk->hclk_isp);
		clk_prepare_enable(clk->isp);
		clk_prepare_enable(clk->isp_jpe);
		clk_prepare_enable(clk->pclkin_isp);
		if (CHIP_TYPE == 3368 || CHIP_TYPE == 3366) {
			clk_prepare_enable(clk->cif_clk_out);
			clk_prepare_enable(clk->pclk_dphyrx);
		} else {
			clk_prepare_enable(clk->clk_mipi_24m);
		}
		clk->in_on = true;

		camsys_trace(1, "%s clock(f: %ld Hz) in turn on",
			dev_name(camsys_dev->miscdev.this_device), isp_clk);
		camsys_mrv_reset_cb(ptr, 1);
		udelay(100);
		camsys_mrv_reset_cb(ptr, 0);
		} else if (!on && clk->in_on) {
		clk_disable_unprepare(clk->aclk_isp);
		clk_disable_unprepare(clk->hclk_isp);
		clk_disable_unprepare(clk->isp);
		clk_disable_unprepare(clk->isp_jpe);
		clk_disable_unprepare(clk->pclkin_isp);
		if (CHIP_TYPE == 3368 || CHIP_TYPE == 3366) {
			clk_disable_unprepare(clk->cif_clk_out);
			clk_disable_unprepare(clk->pclk_dphyrx);
		} else {
			clk_disable_unprepare(clk->clk_mipi_24m);
		}
		/* clk_disable_unprepare(clk->pd_isp); */

		rockchip_clear_system_status(SYS_STATUS_ISP);
		clk->in_on = false;
		camsys_trace(1, "%s clock in turn off",
			dev_name(camsys_dev->miscdev.this_device));
		}
	}

	return 0;
}

static int camsys_mrv_clkout_cb(void *ptr, unsigned int on, unsigned int inclk)
{
	camsys_dev_t *camsys_dev = (camsys_dev_t *)ptr;
	camsys_mrv_clk_t *clk = (camsys_mrv_clk_t *)camsys_dev->clk;

	mutex_lock(&clk->lock);
	if (on && (clk->out_on != on)) {
		clk_set_rate(clk->cif_clk_out, inclk);
		clk_prepare_enable(clk->cif_clk_out);
		clk->out_on = on;
		camsys_trace(1, "%s clock out(rate: %dHz) turn on",
			dev_name(camsys_dev->miscdev.this_device),
					inclk);
	} else if (!on && clk->out_on) {
		if (!IS_ERR_OR_NULL(clk->cif_clk_pll)) {
			clk_set_parent(clk->cif_clk_out,
				clk->cif_clk_pll);
		} else {
			camsys_warn("%s clock out may be not off!",
				dev_name(camsys_dev->miscdev.this_device));
		}

		clk_disable_unprepare(clk->cif_clk_out);
		clk->out_on = 0;

		camsys_trace(1, "%s clock out turn off",
			dev_name(camsys_dev->miscdev.this_device));
	}
	mutex_unlock(&clk->lock);

	return 0;
}
static irqreturn_t camsys_mrv_irq(int irq, void *data)
{
	camsys_dev_t *camsys_dev = (camsys_dev_t *)data;
	camsys_irqstas_t *irqsta;
	camsys_irqpool_t *irqpool;
	unsigned int isp_mis, mipi_mis, mi_mis, *mis, jpg_mis, jpg_err_mis;
	unsigned int mi_ris, mi_imis;
	static unsigned int mipi_frame;

	isp_mis = __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_ISP_MIS));
	mipi_mis = __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MIPI_MIS));
	jpg_mis = __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_JPG_MIS));
	jpg_err_mis = __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_JPG_ERR_MIS));
	mi_mis = __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MI_MIS));

	mi_ris =  __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MI_RIS));
	mi_imis = __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MI_IMIS));
	while ((mi_ris & mi_imis) != mi_mis) {
	camsys_trace(2, "mi_mis status erro,mi_mis 0x%x,"
				"mi_ris 0x%x,imis 0x%x\n",
				mi_mis, mi_ris, mi_imis);
	mi_mis = __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MI_MIS));
	mi_ris =  __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MI_RIS));
	mi_imis = __raw_readl((void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MI_IMIS));
	}

	if (isp_mis & MIS_V_START) {
		mipi_frame = __raw_readl((void *)
				(camsys_dev->devmems.registermem->vir_base +
				 MRV_MIPI_FRAME));
		camsys_trace(2, "mipi_frame: 0x%08x \r\n", mipi_frame);
	}

	__raw_writel(isp_mis, (void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_ISP_ICR));
	__raw_writel(mipi_mis, (void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MIPI_ICR));
	__raw_writel(jpg_mis, (void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_JPG_ICR));
	__raw_writel(jpg_err_mis, (void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_JPG_ERR_ICR));
	__raw_writel(mi_mis, (void volatile *)
				(camsys_dev->devmems.registermem->vir_base +
				MRV_MI_ICR));

	spin_lock(&camsys_dev->irq.lock);
	if (!list_empty(&camsys_dev->irq.irq_pool)) {
		list_for_each_entry(irqpool, &camsys_dev->irq.irq_pool, list) {
			if (irqpool->pid != 0) {
				switch (irqpool->mis) {
				case MRV_ISP_MIS:
				{
					mis = &isp_mis;
					break;
				}

				case MRV_MIPI_MIS:
				{
					mis = &mipi_mis;
					break;
				}
				case MRV_MI_MIS:
				{
					mis = &mi_mis;
					break;
				}

				case MRV_JPG_MIS:
				{
					mis = &jpg_mis;
					break;
				}

				case MRV_JPG_ERR_MIS:
				{
					mis = &jpg_err_mis;
					break;
				}

				default:
				{
					camsys_trace(2,
						"Thread(pid:%d) irqpool mis(%d) is invalidate",
						irqpool->pid, irqpool->mis);
					goto end;
				}
				}

				if (*mis != 0) {
					spin_lock(&irqpool->lock);
					if (!list_empty(&irqpool->deactive)) {
						irqsta =
							list_first_entry(
							&irqpool->deactive,
							camsys_irqstas_t,
							list);
						irqsta->sta.mis = *mis;
						irqsta->sta.fs_id =
							mipi_frame & 0xFFFF;
						irqsta->sta.fe_id =
							(mipi_frame >> 16)
							& 0xFFFF;
						list_del_init(&irqsta->list);
						list_add_tail(&irqsta->list,
							&irqpool->active);
						wake_up(&irqpool->done);
					}
					spin_unlock(&irqpool->lock);
				}
			}
		}
	}
end:
	spin_unlock(&camsys_dev->irq.lock);

	return IRQ_HANDLED;
}

static int camsys_mrv_remove_cb(struct platform_device *pdev)
{
	camsys_dev_t *camsys_dev = platform_get_drvdata(pdev);
	camsys_mrv_clk_t *mrv_clk = NULL;

	if (camsys_dev->clk != NULL) {

		mrv_clk = (camsys_mrv_clk_t *)camsys_dev->clk;
		if (mrv_clk->out_on)
			camsys_mrv_clkout_cb(mrv_clk, 0, 0);
		if (mrv_clk->in_on)
			camsys_mrv_clkin_cb(mrv_clk, 0);

		if (!IS_ERR_OR_NULL(mrv_clk->pd_isp)) {
			devm_clk_put(&pdev->dev, mrv_clk->pd_isp);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp)) {
			devm_clk_put(&pdev->dev, mrv_clk->aclk_isp);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp)) {
			devm_clk_put(&pdev->dev, mrv_clk->hclk_isp);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->isp)) {
			devm_clk_put(&pdev->dev, mrv_clk->isp);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->isp_jpe)) {
			devm_clk_put(&pdev->dev, mrv_clk->isp_jpe);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->pclkin_isp)) {
			devm_clk_put(&pdev->dev, mrv_clk->pclkin_isp);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->cif_clk_out)) {
			devm_clk_put(&pdev->dev, mrv_clk->cif_clk_out);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->clk_vio0_noc)) {
			devm_clk_put(&pdev->dev, mrv_clk->clk_vio0_noc);
		}

		if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp0_noc)) {
			devm_clk_put(&pdev->dev, mrv_clk->hclk_isp0_noc);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp0_wrapper)) {
			devm_clk_put(&pdev->dev, mrv_clk->hclk_isp0_wrapper);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp1_noc)) {
			devm_clk_put(&pdev->dev, mrv_clk->hclk_isp1_noc);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp1_wrapper)) {
			devm_clk_put(&pdev->dev, mrv_clk->hclk_isp1_wrapper);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp0_noc)) {
			devm_clk_put(&pdev->dev, mrv_clk->aclk_isp0_noc);
		}

		if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp0_wrapper)) {
			devm_clk_put(&pdev->dev, mrv_clk->aclk_isp0_wrapper);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp1_noc)) {
			devm_clk_put(&pdev->dev, mrv_clk->aclk_isp1_noc);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp1_wrapper)) {
			devm_clk_put(&pdev->dev, mrv_clk->aclk_isp1_wrapper);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->clk_isp0)) {
			devm_clk_put(&pdev->dev, mrv_clk->clk_isp0);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->clk_isp1)) {
			devm_clk_put(&pdev->dev, mrv_clk->clk_isp1);
		}
		if (!IS_ERR_OR_NULL(mrv_clk->pclkin_isp1)) {
			devm_clk_put(&pdev->dev, mrv_clk->pclkin_isp1);
		}
		if (CHIP_TYPE == 3399)
			pm_runtime_disable(&pdev->dev);
		kfree(mrv_clk);
		mrv_clk = NULL;
	}

	camsys_drm_dma_detach_device(camsys_dev);
	iommu_group_remove_device(&camsys_dev->pdev->dev);
	iommu_put_dma_cookie(camsys_dev->domain);
	iommu_domain_free(camsys_dev->domain);

	return 0;
}
int camsys_mrv_probe_cb(struct platform_device *pdev, camsys_dev_t *camsys_dev)
{
	int err = 0;
	camsys_mrv_clk_t *mrv_clk = NULL;
	struct resource register_res;
	struct iommu_domain *domain = NULL;
	struct iommu_group *group;
	struct device_node *np;

	err = of_address_to_resource(pdev->dev.of_node, 0, &register_res);
	if (err < 0) {
		camsys_err(
			"Get register resource from %s platform device failed!",
			pdev->name);
	}

	err = request_irq(camsys_dev->irq.irq_id, camsys_mrv_irq,
					IRQF_SHARED, CAMSYS_MARVIN_IRQNAME,
					camsys_dev);
	if (err) {
		camsys_err("request irq for %s failed", CAMSYS_MARVIN_IRQNAME);
		goto end;
	}

	/* Clk and Iomux init */
	mrv_clk = kzalloc(sizeof(camsys_mrv_clk_t), GFP_KERNEL);
	if (mrv_clk == NULL) {
		camsys_err("Allocate camsys_mrv_clk_t failed!");
		err = -EINVAL;
		goto clk_failed;
	}
	if (CHIP_TYPE == 3368 || CHIP_TYPE == 3366) {
		/* mrv_clk->pd_isp = devm_clk_get(&pdev->dev, "pd_isp"); */
		mrv_clk->aclk_isp	 = devm_clk_get(&pdev->dev, "aclk_isp");
		mrv_clk->hclk_isp	 = devm_clk_get(&pdev->dev, "hclk_isp");
		mrv_clk->isp		 = devm_clk_get(&pdev->dev, "clk_isp");
		mrv_clk->isp_jpe     = devm_clk_get(&pdev->dev, "clk_isp_jpe");
		mrv_clk->pclkin_isp  = devm_clk_get(&pdev->dev, "pclkin_isp");
		mrv_clk->cif_clk_out = devm_clk_get(&pdev->dev, "clk_cif_out");
		mrv_clk->cif_clk_pll = devm_clk_get(&pdev->dev, "clk_cif_pll");
		mrv_clk->pclk_dphyrx = devm_clk_get(&pdev->dev, "pclk_dphyrx");
		if (CHIP_TYPE == 3368) {
			mrv_clk->clk_vio0_noc =
				devm_clk_get(&pdev->dev, "clk_vio0_noc");
			if (IS_ERR_OR_NULL(mrv_clk->clk_vio0_noc)) {
				camsys_err("Get %s clock resouce failed!\n",
					miscdev_name);
				err = -EINVAL;
				goto clk_failed;
			}

		}

		if (IS_ERR_OR_NULL(mrv_clk->aclk_isp)	 ||
			IS_ERR_OR_NULL(mrv_clk->hclk_isp)	 ||
			IS_ERR_OR_NULL(mrv_clk->isp)		 ||
			IS_ERR_OR_NULL(mrv_clk->isp_jpe)	 ||
			IS_ERR_OR_NULL(mrv_clk->pclkin_isp)  ||
			IS_ERR_OR_NULL(mrv_clk->cif_clk_out) ||
			IS_ERR_OR_NULL(mrv_clk->pclk_dphyrx)) {
			camsys_err("Get %s clock resouce failed!\n",
				miscdev_name);
			err = -EINVAL;
			goto clk_failed;
		}

		clk_set_rate(mrv_clk->isp, 210000000);
		clk_set_rate(mrv_clk->isp_jpe, 210000000);

	} else if (CHIP_TYPE == 3399) {

		pm_runtime_enable(&pdev->dev);
		if (register_res.start == 0xff920000) {
			mrv_clk->hclk_isp1_noc	   =
				devm_clk_get(&pdev->dev, "hclk_isp1_noc");
			mrv_clk->hclk_isp1_wrapper =
				devm_clk_get(&pdev->dev, "hclk_isp1_wrapper");
			mrv_clk->aclk_isp1_noc	   =
				devm_clk_get(&pdev->dev, "aclk_isp1_noc");
			mrv_clk->aclk_isp1_wrapper =
				devm_clk_get(&pdev->dev, "aclk_isp1_wrapper");
			mrv_clk->clk_isp1		   =
				devm_clk_get(&pdev->dev, "clk_isp1");
			mrv_clk->pclkin_isp 	   =
				devm_clk_get(&pdev->dev, "pclk_isp1");
			mrv_clk->pclk_dphytxrx	   =
				devm_clk_get(&pdev->dev, "pclk_dphytxrx");
		} else{
			mrv_clk->hclk_isp0_noc	   =
				devm_clk_get(&pdev->dev, "hclk_isp0_noc");
			mrv_clk->hclk_isp0_wrapper =
				devm_clk_get(&pdev->dev, "hclk_isp0_wrapper");
			mrv_clk->aclk_isp0_noc	   =
				devm_clk_get(&pdev->dev, "aclk_isp0_noc");
			mrv_clk->aclk_isp0_wrapper =
				devm_clk_get(&pdev->dev, "aclk_isp0_wrapper");
			mrv_clk->clk_isp0		   =
				devm_clk_get(&pdev->dev, "clk_isp0");
			mrv_clk->pclk_dphyrx	   =
				devm_clk_get(&pdev->dev, "pclk_dphyrx");
		}
		mrv_clk->cif_clk_out	   =
			devm_clk_get(&pdev->dev, "clk_cif_out");
		mrv_clk->cif_clk_pll	   =
			devm_clk_get(&pdev->dev, "clk_cif_pll");
		mrv_clk->pclk_dphy_ref	   =
			devm_clk_get(&pdev->dev, "pclk_dphy_ref");
		if (register_res.start == 0xff920000) {
			if (IS_ERR_OR_NULL(mrv_clk->hclk_isp1_noc)       ||
				IS_ERR_OR_NULL(mrv_clk->hclk_isp1_wrapper)   ||
				IS_ERR_OR_NULL(mrv_clk->aclk_isp1_noc)       ||
				IS_ERR_OR_NULL(mrv_clk->aclk_isp1_wrapper)   ||
				IS_ERR_OR_NULL(mrv_clk->clk_isp1)            ||
				IS_ERR_OR_NULL(mrv_clk->cif_clk_out)         ||
				IS_ERR_OR_NULL(mrv_clk->cif_clk_pll)         ||
				IS_ERR_OR_NULL(mrv_clk->pclkin_isp)          ||
				IS_ERR_OR_NULL(mrv_clk->pclk_dphytxrx)) {
				camsys_err("Get %s clock resouce failed!\n",
					miscdev_name);
				err = -EINVAL;
				goto clk_failed;
			}
		} else{
			if (IS_ERR_OR_NULL(mrv_clk->hclk_isp0_noc)       ||
				IS_ERR_OR_NULL(mrv_clk->hclk_isp0_wrapper)   ||
				IS_ERR_OR_NULL(mrv_clk->aclk_isp0_noc)       ||
				IS_ERR_OR_NULL(mrv_clk->aclk_isp0_wrapper)   ||
				IS_ERR_OR_NULL(mrv_clk->clk_isp0)            ||
				IS_ERR_OR_NULL(mrv_clk->cif_clk_out)         ||
				IS_ERR_OR_NULL(mrv_clk->cif_clk_pll)         ||
				IS_ERR_OR_NULL(mrv_clk->pclk_dphyrx)) {
				camsys_err("Get %s clock resouce failed!\n",
					miscdev_name);
				err = -EINVAL;
				goto clk_failed;
			}
		}
	} else{
		/*mrv_clk->pd_isp	  =                */
		/*	devm_clk_get(&pdev->dev, "pd_isp");*/
		mrv_clk->aclk_isp	  =
			devm_clk_get(&pdev->dev, "aclk_isp");
		mrv_clk->hclk_isp	  =
			devm_clk_get(&pdev->dev, "hclk_isp");
		mrv_clk->isp		  =
			devm_clk_get(&pdev->dev, "clk_isp");
		mrv_clk->isp_jpe	  =
			devm_clk_get(&pdev->dev, "clk_isp_jpe");
		mrv_clk->pclkin_isp   =
			devm_clk_get(&pdev->dev, "pclkin_isp");
		mrv_clk->cif_clk_out  =
			devm_clk_get(&pdev->dev, "clk_cif_out");
		mrv_clk->cif_clk_pll  =
			devm_clk_get(&pdev->dev, "clk_cif_pll");
		mrv_clk->clk_mipi_24m =
			devm_clk_get(&pdev->dev, "clk_mipi_24m");

		if (
			/*IS_ERR_OR_NULL(mrv_clk->pd_isp)    ||*/
			IS_ERR_OR_NULL(mrv_clk->aclk_isp)    ||
			IS_ERR_OR_NULL(mrv_clk->hclk_isp)    ||
			IS_ERR_OR_NULL(mrv_clk->isp)         ||
			IS_ERR_OR_NULL(mrv_clk->isp_jpe)     ||
			IS_ERR_OR_NULL(mrv_clk->pclkin_isp)  ||
			IS_ERR_OR_NULL(mrv_clk->cif_clk_out) ||
			IS_ERR_OR_NULL(mrv_clk->clk_mipi_24m)) {
			camsys_err("Get %s clock resouce failed!\n",
				miscdev_name);
			err = -EINVAL;
			goto clk_failed;
		}

		clk_set_rate(mrv_clk->isp, 210000000);
		clk_set_rate(mrv_clk->isp_jpe, 210000000);
	}


	mutex_init(&mrv_clk->lock);

	mrv_clk->in_on = false;
	mrv_clk->out_on = 0;

	np = of_parse_phandle(pdev->dev.of_node, "iommus", 0);
	if (np) {
		int index = 0;
		/* iommu domain */
		domain = iommu_domain_alloc(&platform_bus_type);
		if (!domain)
			goto clk_failed;

		err = iommu_get_dma_cookie(domain);
		if (err)
			goto err_free_domain;

		group = iommu_group_get(&pdev->dev);
		if (!group) {
			group = iommu_group_alloc();
			if (IS_ERR(group)) {
				dev_err(&pdev->dev, "Failed to allocate IOMMU group\n");
				goto err_put_cookie;
			}

			err = iommu_group_add_device(group, &pdev->dev);
			iommu_group_put(group);
			if (err) {
				dev_err(&pdev->dev, "failed to add device to IOMMU group\n");
				goto err_put_cookie;
			}
		}
		camsys_dev->domain = domain;
		camsys_dev->dma_buf_cnt = 0;
		camsys_dev->iommu_cb = camsys_mrv_drm_iommu_cb;
		for (index = 0; index < CAMSYS_DMA_BUF_MAX_NUM; index++)
			camsys_dev->dma_buf[index].fd = -1;
	} else {
		camsys_dev->iommu_cb = camsys_mrv_iommu_cb;
	}

	camsys_dev->clk = (void *)mrv_clk;
	camsys_dev->clkin_cb = camsys_mrv_clkin_cb;
	camsys_dev->clkout_cb = camsys_mrv_clkout_cb;
	camsys_dev->reset_cb = camsys_mrv_reset_cb;
	camsys_dev->iomux = camsys_mrv_iomux_cb;
	camsys_dev->flash_trigger_cb = camsys_mrv_flash_trigger_cb;

	camsys_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	camsys_dev->miscdev.name = miscdev_name;
	camsys_dev->miscdev.nodename = miscdev_name;
	camsys_dev->miscdev.fops = &camsys_fops;

	if (CHIP_TYPE == 3399) {
		if (register_res.start == 0xff920000) {
			camsys_dev->miscdev.name = "camsys_marvin1";
			camsys_dev->miscdev.nodename = "camsys_marvin1";
		}
	}

	err = misc_register(&camsys_dev->miscdev);
	if (err < 0) {
		camsys_err("misc register %s failed!", miscdev_name);
		goto misc_register_failed;
	}
	/* Variable init */
	camsys_dev->dev_id = CAMSYS_DEVID_MARVIN;
	camsys_dev->platform_remove = camsys_mrv_remove_cb;

	return 0;
misc_register_failed:
	if (!IS_ERR_OR_NULL(camsys_dev->miscdev.this_device))
		misc_deregister(&camsys_dev->miscdev);
err_put_cookie:
	if (domain)
		iommu_put_dma_cookie(domain);
err_free_domain:
	if (domain)
		iommu_domain_free(domain);
clk_failed:
	if (mrv_clk != NULL) {
		if (!IS_ERR_OR_NULL(mrv_clk->pd_isp))
			clk_put(mrv_clk->pd_isp);

		if (!IS_ERR_OR_NULL(mrv_clk->aclk_isp))
			clk_put(mrv_clk->aclk_isp);

		if (!IS_ERR_OR_NULL(mrv_clk->hclk_isp))
			clk_put(mrv_clk->hclk_isp);

		if (!IS_ERR_OR_NULL(mrv_clk->isp))
			clk_put(mrv_clk->isp);

		if (!IS_ERR_OR_NULL(mrv_clk->isp_jpe))
			clk_put(mrv_clk->isp_jpe);

		if (!IS_ERR_OR_NULL(mrv_clk->pclkin_isp))
			clk_put(mrv_clk->pclkin_isp);

		if (!IS_ERR_OR_NULL(mrv_clk->cif_clk_out))
			clk_put(mrv_clk->cif_clk_out);

		if (CHIP_TYPE == 3368 || CHIP_TYPE == 3366) {
			if (!IS_ERR_OR_NULL(mrv_clk->pclk_dphyrx))
				clk_put(mrv_clk->pclk_dphyrx);

			if (!IS_ERR_OR_NULL(mrv_clk->clk_vio0_noc))
				clk_put(mrv_clk->clk_vio0_noc);
		}

		kfree(mrv_clk);
		mrv_clk = NULL;
	}

end:
	return err;
}
EXPORT_SYMBOL_GPL(camsys_mrv_probe_cb);

