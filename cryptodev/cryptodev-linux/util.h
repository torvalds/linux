int sg_copy(struct scatterlist *sg_from, struct scatterlist *sg_to, int len);
struct scatterlist *sg_advance(struct scatterlist *sg, int consumed);
