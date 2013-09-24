obj-$(CONFIG_RK_PM_TESTS) += rk_pm_test.o
rk_pm_test-y += rk_pm_tests.o

# Optional functions
# set_volt: change dvfs_table to change voltage
rk_pm_test-$(CONFIG_PM_TEST_CLK_RATE) += clk_rate.o
rk_pm_test-$(CONFIG_PM_TEST_CLK_VOLT) += clk_volt.o
rk_pm_test-$(CONFIG_PM_TEST_MAXFREQ) += maxfreq.o
rk_pm_test-$(CONFIG_PM_TEST_FREQ_LIMIT) += freq_limit.o
rk_pm_test-$(CONFIG_PM_TEST_CPU_USAGE) += cpu_usage.o
rk_pm_test-$(CONFIG_PM_TEST_SUSPEND_DBG) += rk_suspend_test.o
rk_pm_test-$(CONFIG_PM_TEST_CLK_AUTO_VOLT) += clk_auto_volt.o
rk_pm_test-$(CONFIG_PM_TEST_DELAYLINE) += delayline.o
rk_pm_test-$(CONFIG_PM_TEST_DVFS_TABLE_SCAN) += dvfs_table_scan.o
rk_pm_test-$(CONFIG_PM_TEST_DVFS_TABLE_RESET) += dvfs_table_reset.o
obj-$(CONFIG_PM_TEST_FT) += ft/
