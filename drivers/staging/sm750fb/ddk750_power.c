#include "ddk750_help.h"
#include "ddk750_reg.h"
#include "ddk750_power.h"

void ddk750_setDPMS(DPMS_t state)
{
	unsigned int value;
	if(getChipType() == SM750LE){
		value = PEEK32(CRT_DISPLAY_CTRL);
		POKE32(CRT_DISPLAY_CTRL,FIELD_VALUE(value,CRT_DISPLAY_CTRL,DPMS,state));
	}else{
		value = PEEK32(SYSTEM_CTRL);
		value= FIELD_VALUE(value,SYSTEM_CTRL,DPMS,state);
		POKE32(SYSTEM_CTRL, value);
	}
}

unsigned int getPowerMode(void)
{
	if(getChipType() == SM750LE)
		return 0;
    return (FIELD_GET(PEEK32(POWER_MODE_CTRL), POWER_MODE_CTRL, MODE));
}


/*
 * SM50x can operate in one of three modes: 0, 1 or Sleep.
 * On hardware reset, power mode 0 is default.
 */
void setPowerMode(unsigned int powerMode)
{
    unsigned int control_value = 0;

    control_value = PEEK32(POWER_MODE_CTRL);

	if(getChipType() == SM750LE)
		return;

    switch (powerMode)
    {
        case POWER_MODE_CTRL_MODE_MODE0:
            control_value = FIELD_SET(control_value, POWER_MODE_CTRL, MODE, MODE0);
            break;

        case POWER_MODE_CTRL_MODE_MODE1:
            control_value = FIELD_SET(control_value, POWER_MODE_CTRL, MODE, MODE1);
            break;

        case POWER_MODE_CTRL_MODE_SLEEP:
            control_value = FIELD_SET(control_value, POWER_MODE_CTRL, MODE, SLEEP);
            break;

        default:
            break;
    }

    /* Set up other fields in Power Control Register */
    if (powerMode == POWER_MODE_CTRL_MODE_SLEEP)
    {
        control_value =
#ifdef VALIDATION_CHIP
            FIELD_SET(  control_value, POWER_MODE_CTRL, 336CLK, OFF) |
#endif
            FIELD_SET(  control_value, POWER_MODE_CTRL, OSC_INPUT,  OFF);
    }
    else
    {
        control_value =
#ifdef VALIDATION_CHIP
            FIELD_SET(  control_value, POWER_MODE_CTRL, 336CLK, ON) |
#endif
            FIELD_SET(  control_value, POWER_MODE_CTRL, OSC_INPUT,  ON);
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

    switch (mode)
    {
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
    uint32_t gate;

    gate = PEEK32(CURRENT_GATE);
    if (enable)
    {
        gate = FIELD_SET(gate, CURRENT_GATE, DE,  ON);
        gate = FIELD_SET(gate, CURRENT_GATE, CSC, ON);
    }
    else
    {
        gate = FIELD_SET(gate, CURRENT_GATE, DE,  OFF);
        gate = FIELD_SET(gate, CURRENT_GATE, CSC, OFF);
    }

    setCurrentGate(gate);
}


/*
 * This function enable/disable the ZV Port.
 */
void enableZVPort(unsigned int enable)
{
    uint32_t gate;

    /* Enable ZV Port Gate */
    gate = PEEK32(CURRENT_GATE);
    if (enable)
    {
        gate = FIELD_SET(gate, CURRENT_GATE, ZVPORT, ON);
#if 1
        /* Using Software I2C */
        gate = FIELD_SET(gate, CURRENT_GATE, GPIO, ON);
#else
        /* Using Hardware I2C */
        gate = FIELD_SET(gate, CURRENT_GATE, I2C,    ON);
#endif
    }
    else
    {
        /* Disable ZV Port Gate. There is no way to know whether the GPIO pins are being used
           or not. Therefore, do not disable the GPIO gate. */
        gate = FIELD_SET(gate, CURRENT_GATE, ZVPORT, OFF);
    }

    setCurrentGate(gate);
}


void enableSSP(unsigned int enable)
{
    uint32_t gate;

    /* Enable SSP Gate */
    gate = PEEK32(CURRENT_GATE);
    if (enable)
        gate = FIELD_SET(gate, CURRENT_GATE, SSP, ON);
    else
        gate = FIELD_SET(gate, CURRENT_GATE, SSP, OFF);

    setCurrentGate(gate);
}

void enableDMA(unsigned int enable)
{
    uint32_t gate;

    /* Enable DMA Gate */
    gate = PEEK32(CURRENT_GATE);
    if (enable)
        gate = FIELD_SET(gate, CURRENT_GATE, DMA, ON);
    else
        gate = FIELD_SET(gate, CURRENT_GATE, DMA, OFF);

    setCurrentGate(gate);
}

/*
 * This function enable/disable the GPIO Engine
 */
void enableGPIO(unsigned int enable)
{
    uint32_t gate;

    /* Enable GPIO Gate */
    gate = PEEK32(CURRENT_GATE);
    if (enable)
        gate = FIELD_SET(gate, CURRENT_GATE, GPIO, ON);
    else
        gate = FIELD_SET(gate, CURRENT_GATE, GPIO, OFF);

    setCurrentGate(gate);
}

/*
 * This function enable/disable the PWM Engine
 */
void enablePWM(unsigned int enable)
{
    uint32_t gate;

    /* Enable PWM Gate */
    gate = PEEK32(CURRENT_GATE);
    if (enable)
        gate = FIELD_SET(gate, CURRENT_GATE, PWM, ON);
    else
        gate = FIELD_SET(gate, CURRENT_GATE, PWM, OFF);

    setCurrentGate(gate);
}

/*
 * This function enable/disable the I2C Engine
 */
void enableI2C(unsigned int enable)
{
    uint32_t gate;

    /* Enable I2C Gate */
    gate = PEEK32(CURRENT_GATE);
    if (enable)
        gate = FIELD_SET(gate, CURRENT_GATE, I2C, ON);
    else
        gate = FIELD_SET(gate, CURRENT_GATE, I2C, OFF);

    setCurrentGate(gate);
}


