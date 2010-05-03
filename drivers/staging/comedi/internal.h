
int comedi_alloc_board_minor(struct device *hardware_device);
void comedi_free_board_minor(unsigned minor);
void comedi_reset_async_buf(struct comedi_async *async);
