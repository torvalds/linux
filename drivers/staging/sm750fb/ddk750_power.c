#include "ddk750_help.h"
#include "ddk750_reg.h"
#include "ddk750_power.h"

void ddk750_setDPMS(DPMS_t state)
{
	unsigned int value;

	if (sm750_get_chip_type() == SM750LE) {
		value = PEEK32(CRT_DISPLAY_CTRL) & ~CRT_DISPLAY_CTRL_DPMS_MASK;
		value |= (state << CRT_DISPLAY_CTRL_DPMS_SHIFT);
		POKE32(CRT_DISPLAY_CTRL, value);
	} else {
		value = PEEK32(SYSTEM_CTRL);
		value = (value & ~SYSTEM_CTRL_DPMS_MASK) | state;
		POKE32(SYSTEM_CTRL, value);
	}
}

static unsigned int getPowerMode(void)
{
	if (sm750_get_chip_type() == SM750LE)
		return 0;
	return PEEK32(POWER_MODE_CTRL) & POWER_MODE_CTRL_MODE_MASK;
}


/*
 * SM50x can operate in one of three modes: 0, 1 or Sleep.
 * On hardware reset, power mode 0 is default.
 */
void setPowerMode(unsigned int powerMode)
{
	unsigned int control_value = 0;

	control_value = PEEK32(POWER_MODE_CTRL) & ~POWER_MODE_CTRL_MODE_MASK;

	if (sm750_get_chip_type() == SM750LE)
		return;

	switch (powerMode) {
	case POWER_MODE_CTRL_MODE_MODE0:
		control_value |= POWER_MODE_CTRL_MODE_MODE0;
		break;

	case POWER_MODE_CTRL_MODE_MODE1:
		control_value |= POWER_MODE_CTRL_MODE_MODE1;
		break;

	case POWER_MODE_CTRL_MODE_SLEEP:
		control_value |= POWER_MODE_CTRL_MODE_SLEEP;
		break;

	default:
		break;
	}

	/* Set up other fields in Power Control Register */
	if (powerMode == POWER_MODE_CTRL_MODE_SLEEP) {
		control_value &= ~POWER_MODE_CTRL_OSC_INPUT;
#ifdef VALIDATION_CHIP
		control_value &= ~POWER_MODE_CTRL_336CLK;
#endif
	} else {
		control_value |= POWER_MODE_CTRL_OSC_INPUT;
#ifdef VALIDATION_CHIP
		control_value |= POWER_MODE_CTRL_336CLK;
#endif
	}

	/* Program new power mode. */
	POKE32(POWER_MODE_CTRL, control_value);
}

void setCurrentGate(unsigned int gate)
{
	unsigned int gate_reg;
	unsigned int mode;

	/* Get current power mode. */
	mode = getPowerMode();

	switch (mode) {
	case POWER_MODE_CTRL_MODE_MODE0:
		gate_reg = MODE0_GATE;
		break;

	case POWER_MODE_CTRL_MODE_MODE1:
		gate_reg = MODE1_GATE;
		break;

	default:
		gate_reg = MODE0_GATE;
		break;
	}
	POKE32(gate_reg, gate);
}



/*
 * This function enable/disable the 2D engine.
 */
void enable2DEngine(unsigned int enable)
{
	u32 gate;

	gate = PEEK32(CURRENT_GATE);
	if (enable)
		gate |= (CURRENT_GATE_DE | CURRENT_GATE_CSC);
	else
		gate &= ~(CURRENT_GATE_DE | CURRENT_GATE_CSC);

	setCurrentGate(gate);
}

void enableDMA(unsigned int enable)
{
	u32 gate;

	/* Enable DMA Gate */
	gate = PEEK32(CURRENT_GATE);
	if (enable)
		gate |= CURRENT_GATE_DMA;
	else
		gate &= ~CURRENT_GATE_DMA;

	setCurrentGate(gate);
}

/*
 * This function enable/disable the GPIO Engine
 */
void enableGPIO(unsigned int enable)
{
	u32 gate;

	/* Enable GPIO Gate */
	gate = PEEK32(CURRENT_GATE);
	if (enable)
		gate |= CURRENT_GATE_GPIO;
	else
		gate &= ~CURRENT_GATE_GPIO;

	setCurrentGate(gate);
}

/*
 * This function enable/disable the I2C Engine
 */
void enableI2C(unsigned int enable)
{
	u32 gate;

	/* Enable I2C Gate */
	gate = PEEK32(CURRENT_GATE);
	if (enable)
		gate |= CURRENT_GATE_I2C;
	else
		gate &= ~CURRENT_GATE_I2C;

	setCurrentGate(gate);
}


