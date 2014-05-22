/* Memory Debugging */
#ifdef DWC_DEBUG_MEMORY

#include "dwc_os.h"
#include "dwc_list.h"

struct allocation {
	void *addr;
	void *ctx;
	char *func;
	int line;
	uint32_t size;
	int dma;
	DWC_CIRCLEQ_ENTRY(allocation) entry;
};

DWC_CIRCLEQ_HEAD(allocation_queue, allocation);

struct allocation_manager {
	void *mem_ctx;
	struct allocation_queue allocations;

	/* statistics */
	int num;
	int num_freed;
	int num_active;
	uint32_t total;
	uint32_t cur;
	uint32_t max;
};

static struct allocation_manager *manager;

static int add_allocation(void *ctx, uint32_t size, char const *func, int line, void *addr,
			  int dma)
{
	struct allocation *a;

	DWC_ASSERT(manager != NULL, "manager not allocated");

	a = __DWC_ALLOC_ATOMIC(manager->mem_ctx, sizeof(*a));
	if (!a) {
		return -DWC_E_NO_MEMORY;
	}

	a->func = __DWC_ALLOC_ATOMIC(manager->mem_ctx, DWC_STRLEN(func) + 1);
	if (!a->func) {
		__DWC_FREE(manager->mem_ctx, a);
		return -DWC_E_NO_MEMORY;
	}

	DWC_MEMCPY(a->func, func, DWC_STRLEN(func) + 1);
	a->addr = addr;
	a->ctx = ctx;
	a->line = line;
	a->size = size;
	a->dma = dma;
	DWC_CIRCLEQ_INSERT_TAIL(&manager->allocations, a, entry);

	/* Update stats */
	manager->num++;
	manager->num_active++;
	manager->total += size;
	manager->cur += size;

	if (manager->max < manager->cur) {
		manager->max = manager->cur;
	}

	return 0;
}

static struct allocation *find_allocation(void *ctx, void *addr)
{
	struct allocation *a;

	DWC_CIRCLEQ_FOREACH(a, &manager->allocations, entry) {
		if (a->ctx == ctx && a->addr == addr) {
			return a;
		}
	}

	return NULL;
}

static void free_allocation(void *ctx, void *addr, char const *func, int line)
{
	struct allocation *a = find_allocation(ctx, addr);

	if (!a) {
		DWC_ASSERT(0,
			   "Free of address %p that was never allocated or already freed %s:%d",
			   addr, func, line);
		return;
	}

	DWC_CIRCLEQ_REMOVE(&manager->allocations, a, entry);

	manager->num_active--;
	manager->num_freed++;
	manager->cur -= a->size;
	__DWC_FREE(manager->mem_ctx, a->func);
	__DWC_FREE(manager->mem_ctx, a);
}

int dwc_memory_debug_start(void *mem_ctx)
{
	DWC_ASSERT(manager == NULL, "Memory debugging has already started\n");

	if (manager) {
		return -DWC_E_BUSY;
	}

	manager = __DWC_ALLOC(mem_ctx, sizeof(*manager));
	if (!manager) {
		return -DWC_E_NO_MEMORY;
	}

	DWC_CIRCLEQ_INIT(&manager->allocations);
	manager->mem_ctx = mem_ctx;
	manager->num = 0;
	manager->num_freed = 0;
	manager->num_active = 0;
	manager->total = 0;
	manager->cur = 0;
	manager->max = 0;

	return 0;
}

void dwc_memory_debug_stop(void)
{
	struct allocation *a;

	dwc_memory_debug_report();

	DWC_CIRCLEQ_FOREACH(a, &manager->allocations, entry) {
		DWC_ERROR("Memory leaked from %s:%d\n", a->func, a->line);
		free_allocation(a->ctx, a->addr, NULL, -1);
	}

	__DWC_FREE(manager->mem_ctx, manager);
}

void dwc_memory_debug_report(void)
{
	struct allocation *a;

	DWC_PRINTF("\n\n\n----------------- Memory Debugging Report -----------------\n\n");
	DWC_PRINTF("Num Allocations = %d\n", manager->num);
	DWC_PRINTF("Freed = %d\n", manager->num_freed);
	DWC_PRINTF("Active = %d\n", manager->num_active);
	DWC_PRINTF("Current Memory Used = %d\n", manager->cur);
	DWC_PRINTF("Total Memory Used = %d\n", manager->total);
	DWC_PRINTF("Maximum Memory Used at Once = %d\n", manager->max);
	DWC_PRINTF("Unfreed allocations:\n");

	DWC_CIRCLEQ_FOREACH(a, &manager->allocations, entry) {
		DWC_PRINTF("    addr=%p, size=%d from %s:%d, DMA=%d\n",
			   a->addr, a->size, a->func, a->line, a->dma);
	}
}

/* The replacement functions */
void *dwc_alloc_debug(void *mem_ctx, uint32_t size, char const *func, int line)
{
	void *addr = __DWC_ALLOC(mem_ctx, size);

	if (!addr) {
		return NULL;
	}

	if (add_allocation(mem_ctx, size, func, line, addr, 0)) {
		__DWC_FREE(mem_ctx, addr);
		return NULL;
	}

	return addr;
}

void *dwc_alloc_atomic_debug(void *mem_ctx, uint32_t size, char const *func,
			     int line)
{
	void *addr = __DWC_ALLOC_ATOMIC(mem_ctx, size);

	if (!addr) {
		return NULL;
	}

	if (add_allocation(mem_ctx, size, func, line, addr, 0)) {
		__DWC_FREE(mem_ctx, addr);
		return NULL;
	}

	return addr;
}

void dwc_free_debug(void *mem_ctx, void *addr, char const *func, int line)
{
	free_allocation(mem_ctx, addr, func, line);
	__DWC_FREE(mem_ctx, addr);
}

void *dwc_dma_alloc_debug(void *dma_ctx, uint32_t size, dwc_dma_t *dma_addr,
			  char const *func, int line)
{
	void *addr = __DWC_DMA_ALLOC(dma_ctx, size, dma_addr);

	if (!addr) {
		return NULL;
	}

	if (add_allocation(dma_ctx, size, func, line, addr, 1)) {
		__DWC_DMA_FREE(dma_ctx, size, addr, *dma_addr);
		return NULL;
	}

	return addr;
}

void *dwc_dma_alloc_atomic_debug(void *dma_ctx, uint32_t size,
				 dwc_dma_t *dma_addr, char const *func, int line)
{
	void *addr = __DWC_DMA_ALLOC_ATOMIC(dma_ctx, size, dma_addr);

	if (!addr) {
		return NULL;
	}

	if (add_allocation(dma_ctx, size, func, line, addr, 1)) {
		__DWC_DMA_FREE(dma_ctx, size, addr, *dma_addr);
		return NULL;
	}

	return addr;
}

void dwc_dma_free_debug(void *dma_ctx, uint32_t size, void *virt_addr,
			dwc_dma_t dma_addr, char const *func, int line)
{
	free_allocation(dma_ctx, virt_addr, func, line);
	__DWC_DMA_FREE(dma_ctx, size, virt_addr, dma_addr);
}

#endif /* DWC_DEBUG_MEMORY */
