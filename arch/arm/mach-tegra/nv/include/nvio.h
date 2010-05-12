#define tegra_apertures(_aperture)                      \
        _aperture(IRAM,         0x40000000, SZ_1M)      \
        _aperture(HOST1X,       0x50000000, SZ_1M)      \
        _aperture(PPSB,         0x60000000, SZ_1M)      \
        _aperture(APB,          0x70000000, SZ_1M)      \
        _aperture(USB,          0xC5000000, SZ_1M)      \
        _aperture(SDIO,         0xC8000000, SZ_1M)

/* remaps USB to 0xFE9xxxxx, SDIO to 0xFECxxxxx, and everything else to
 * 0xFEnxxxxx, where n is the most significant nybble */
#define tegra_munge_pa(_pa)                                             \
        (((((_pa)&0x70000000UL)>>8) + (((_pa)&0x0F000000UL)>>4)) |      \
         ((_pa)&0xFFFFFUL) | 0xFE000000UL )
