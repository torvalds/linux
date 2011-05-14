
#include <linux/kfifo.h>
#include "iio.h"
#include "ring_generic.h"

struct iio_kfifo {
	struct iio_ring_buffer ring;
	struct kfifo kf;
	int use_count;
	int update_needed;
	struct mutex use_lock;
};

#define iio_to_kfifo(r) container_of(r, struct iio_kfifo, ring)

int iio_create_kfifo(struct iio_ring_buffer **r);
int iio_init_kfifo(struct iio_ring_buffer *r, struct iio_dev *indio_dev);
void iio_exit_kfifo(struct iio_ring_buffer *r);
void iio_free_kfifo(struct iio_ring_buffer *r);
void iio_mark_kfifo_in_use(struct iio_ring_buffer *r);
void iio_unmark_kfifo_in_use(struct iio_ring_buffer *r);

int iio_store_to_kfifo(struct iio_ring_buffer *r, u8 *data, s64 timestamp);
int iio_rip_kfifo(struct iio_ring_buffer *r,
		size_t count,
		char __user *buf,
		int *dead_offset);

int iio_request_update_kfifo(struct iio_ring_buffer *r);
int iio_mark_update_needed_kfifo(struct iio_ring_buffer *r);

int iio_get_bytes_per_datum_kfifo(struct iio_ring_buffer *r);
int iio_set_bytes_per_datum_kfifo(struct iio_ring_buffer *r, size_t bpd);
int iio_get_length_kfifo(struct iio_ring_buffer *r);
int iio_set_length_kfifo(struct iio_ring_buffer *r, int length);

static inline void iio_kfifo_register_funcs(struct iio_ring_access_funcs *ra)
{
	ra->mark_in_use = &iio_mark_kfifo_in_use;
	ra->unmark_in_use = &iio_unmark_kfifo_in_use;

	ra->store_to = &iio_store_to_kfifo;
	ra->rip_lots = &iio_rip_kfifo;

	ra->mark_param_change = &iio_mark_update_needed_kfifo;
	ra->request_update = &iio_request_update_kfifo;

	ra->get_bytes_per_datum = &iio_get_bytes_per_datum_kfifo;
	ra->set_bytes_per_datum = &iio_set_bytes_per_datum_kfifo;
	ra->get_length = &iio_get_length_kfifo;
	ra->set_length = &iio_set_length_kfifo;
};

struct iio_ring_buffer *iio_kfifo_allocate(struct iio_dev *indio_dev);
void iio_kfifo_free(struct iio_ring_buffer *r);

