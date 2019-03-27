pcap_t *rdmasniff_create(const char *device, char *ebuf, int *is_ours);
int rdmasniff_findalldevs(pcap_if_list_t *devlistp, char *err_str);
