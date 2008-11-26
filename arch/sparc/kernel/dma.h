void *sbus_alloc_consistent(struct device *dev, long len, u32 *dma_addrp);
void sbus_free_consistent(struct device *dev, long n, void *p, u32 ba);
dma_addr_t sbus_map_single(struct device *dev, void *va,
			   size_t len, int direction);
void sbus_unmap_single(struct device *dev, dma_addr_t ba,
		       size_t n, int direction);
int sbus_map_sg(struct device *dev, struct scatterlist *sg,
		int n, int direction);
void sbus_unmap_sg(struct device *dev, struct scatterlist *sg,
		   int n, int direction);
void sbus_dma_sync_single_for_cpu(struct device *dev, dma_addr_t ba,
				  size_t size, int direction);
void sbus_dma_sync_single_for_device(struct device *dev, dma_addr_t ba,
				     size_t size, int direction);
