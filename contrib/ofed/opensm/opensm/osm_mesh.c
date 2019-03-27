/*
 * Copyright (c) 2008-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2008,2009      System Fabric Works, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *      routines to analyze certain meshes
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_MESH_C
#include <opensm/osm_switch.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_mesh.h>
#include <opensm/osm_ucast_lash.h>

#define MAX_DEGREE	(8)
#define MAX_DIMENSION	(8)
#define LARGE		(0x7fffffff)

/*
 * characteristic polynomials for selected 1d through 8d tori
 */
static const struct mesh_info {
	int dimension;			/* dimension of the torus */
	int size[MAX_DIMENSION];	/* size of the torus */
	unsigned int degree;		/* degree of polynomial */
	int poly[MAX_DEGREE+1];		/* polynomial */
} mesh_info[] = {
	{0, {0},       0, {0},					},

	{1, {2},       1, {0, -1},				},
	{1, {3},       2, {-1, 0, 1},				},
	{1, {5},       2, {-9, 0, 1},				},
	{1, {6},       2, {-36, 0, 1},				},

	{2, {2, 2},    2, {-4, 0, 1},				},
	{2, {3, 2},    3, {8, 9, 0, -1},			},
	{2, {5, 2},    3, {24, 17, 0, -1},			},
	{2, {6, 2},    3, {32, 24, 0, -1},			},
	{2, {3, 3},    4, {-15, -32, -18, 0, 1},		},
	{2, {5, 3},    4, {-39, -64, -26, 0, 1},		},
	{2, {6, 3},    4, {-48, -80, -33, 0, 1},		},
	{2, {5, 5},    4, {-63, -96, -34, 0, 1},		},
	{2, {6, 5},    4, {-48, -112, -41, 0, 1},		},
	{2, {6, 6},    4, {0, -128, -48, 0, 1},			},

	{3, {2, 2, 2}, 3, {16, 12, 0, -1},			},
	{3, {3, 2, 2}, 4, {-28, -48, -21, 0, 1},		},
	{3, {5, 2, 2}, 4, {-60, -80, -29, 0, 1},		},
	{3, {6, 2, 2}, 4, {-64, -96, -36, 0, 1},		},
	{3, {3, 3, 2}, 5, {48, 127, 112, 34, 0, -1},		},
	{3, {5, 3, 2}, 5, {96, 215, 160, 42, 0, -1},		},
	{3, {6, 3, 2}, 5, {96, 232, 184, 49, 0, -1},		},
	{3, {5, 5, 2}, 5, {144, 303, 208, 50, 0, -1},		},
	{3, {6, 5, 2}, 5, {96, 296, 232, 57, 0, -1},		},
	{3, {6, 6, 2}, 5, {0, 256, 256, 64, 0, -1},		},
	{3, {3, 3, 3}, 6, {-81, -288, -381, -224, -51, 0, 1},	},
	{3, {5, 3, 3}, 6, {-153, -480, -557, -288, -59, 0, 1},	},
	{3, {6, 3, 3}, 6, {-144, -480, -591, -320, -66, 0, 1},	},
	{3, {5, 5, 3}, 6, {-225, -672, -733, -352, -67, 0, 1},	},
	{3, {6, 5, 3}, 6, {-144, -576, -743, -384, -74, 0, 1},	},
	{3, {6, 6, 3}, 6, {0, -384, -720, -416, -81, 0, 1},	},
	{3, {5, 5, 5}, 6, {-297, -864, -909, -416, -75, 0, 1},	},
	{3, {6, 5, 5}, 6, {-144, -672, -895, -448, -82, 0, 1},	},
	{3, {6, 6, 5}, 6, {0, -384, -848, -480, -89, 0, 1},	},
	{3, {6, 6, 6}, 6, {0, 0, -768, -512, -96, 0, 1},	},

	{4, {2, 2, 2, 2},	4, {-48, -64, -24, 0, 1},	},
	{4, {3, 2, 2, 2},	5, {80, 180, 136, 37, 0, -1},	},
	{4, {5, 2, 2, 2},	5, {144, 276, 184, 45, 0, -1},	},
	{4, {6, 2, 2, 2},	5, {128, 288, 208, 52, 0, -1},	},
	{4, {3, 3, 2, 2},	6, {-132, -416, -487, -256, -54, 0, 1},	},
	{4, {5, 3, 2, 2},	6, {-228, -640, -671, -320, -62, 0, 1},	},
	{4, {6, 3, 2, 2},	6, {-192, -608, -700, -352, -69, 0, 1},	},
	{4, {5, 5, 2, 2},	6, {-324, -864, -855, -384, -70, 0, 1},	},
	{4, {6, 5, 2, 2},	6, {-192, -736, -860, -416, -77, 0, 1},	},
	{4, {6, 6, 2, 2},	6, {0, -512, -832, -448, -84, 0, 1},	},
	{4, {3, 3, 3, 2},	7, {216, 873, 1392, 1101, 440, 75, 0, -1},	},
	{4, {5, 3, 3, 2},	7, {360, 1329, 1936, 1405, 520, 83, 0, -1},	},
	{4, {6, 3, 3, 2},	7, {288, 1176, 1872, 1455, 560, 90, 0, -1},	},
	{4, {5, 5, 3, 2},	7, {504, 1785, 2480, 1709, 600, 91, 0, -1},	},
	{4, {6, 5, 3, 2},	7, {288, 1368, 2272, 1735, 640, 98, 0, -1},	},
	{4, {6, 6, 3, 2},	7, {0, 768, 1920, 1728, 680, 105, 0, -1},	},
	{4, {5, 5, 5, 2},	7, {648, 2241, 3024, 2013, 680, 99, 0, -1},	},
	{4, {6, 5, 5, 2},	7, {288, 1560, 2672, 2015, 720, 106, 0, -1},	},
	{4, {6, 6, 5, 2},	7, {0, 768, 2176, 1984, 760, 113, 0, -1},	},
	{4, {6, 6, 6, 2},	7, {0, 0, 1536, 1920, 800, 120, 0, -1},	},
	{4, {3, 3, 3, 3},	8, {-351, -1728, -3492, -3712, -2202, -704, -100, 0, 1},	},
	{4, {5, 3, 3, 3},	8, {-567, -2592, -4860, -4800, -2658, -800, -108, 0, 1},	},
	{4, {6, 3, 3, 3},	8, {-432, -2160, -4401, -4672, -2733, -848, -115, 0, 1},	},
	{4, {5, 5, 3, 3},	8, {-783, -3456, -6228, -5888, -3114, -896, -116, 0, 1},	},
	{4, {6, 5, 3, 3},	8, {-432, -2448, -5241, -5568, -3165, -944, -123, 0, 1},	},
	{4, {6, 6, 3, 3},	8, {0, -1152, -3888, -5056, -3183, -992, -130, 0, 1},	},
	{4, {5, 5, 5, 3},	8, {-999, -4320, -7596, -6976, -3570, -992, -124, 0, 1},	},
	{4, {6, 5, 5, 3},	8, {-432, -2736, -6081, -6464, -3597, -1040, -131, 0, 1},	},
	{4, {6, 6, 5, 3},	8, {0, -1152, -4272, -5760, -3591, -1088, -138, 0, 1},	},
	{4, {6, 6, 6, 3},	8, {0, 0, -2304, -4864, -3552, -1136, -145, 0, 1},	},

	{5, {2, 2, 2, 2, 2},	5, {128, 240, 160, 40, 0, -1},	},
	{5, {3, 2, 2, 2, 2},	6, {-208, -576, -600, -288, -57, 0, 1},	},
	{5, {5, 2, 2, 2, 2},	6, {-336, -832, -792, -352, -65, 0, 1},	},
	{5, {6, 2, 2, 2, 2},	6, {-256, -768, -816, -384, -72, 0, 1},	},
	{5, {3, 3, 2, 2, 2},	7, {336, 1228, 1776, 1287, 480, 78, 0, -1},	},
	{5, {5, 3, 2, 2, 2},	7, {528, 1772, 2368, 1599, 560, 86, 0, -1},	},
	{5, {6, 3, 2, 2, 2},	7, {384, 1504, 2256, 1644, 600, 93, 0, -1},	},
	{5, {5, 5, 2, 2, 2},	7, {720, 2316, 2960, 1911, 640, 94, 0, -1},	},
	{5, {6, 5, 2, 2, 2},	7, {384, 1760, 2704, 1932, 680, 101, 0, -1},	},
	{5, {6, 6, 2, 2, 2},	7, {0, 1024, 2304, 1920, 720, 108, 0, -1},	},
	{5, {3, 3, 3, 2, 2},	8, {-540, -2448, -4557, -4480, -2481, -752, -103, 0, 1},	},
	{5, {5, 3, 3, 2, 2},	8, {-828, -3504, -6101, -5632, -2945, -848, -111, 0, 1},	},
	{5, {6, 3, 3, 2, 2},	8, {-576, -2784, -5412, -5440, -3015, -896, -118, 0, 1},	},
	{5, {5, 5, 3, 2, 2},	8, {-1116, -4560, -7645, -6784, -3409, -944, -119, 0, 1},	},
	{5, {6, 5, 3, 2, 2},	8, {-576, -3168, -6404, -6400, -3455, -992, -126, 0, 1},	},
	{5, {6, 6, 3, 2, 2},	8, {0, -1536, -4800, -5824, -3468, -1040, -133, 0, 1},	},
	{5, {5, 5, 5, 2, 2},	8, {-1404, -5616, -9189, -7936, -3873, -1040, -127, 0, 1},	},
	{5, {6, 5, 5, 2, 2},	8, {-576, -3552, -7396, -7360, -3895, -1088, -134, 0, 1},	},
	{5, {6, 6, 5, 2, 2},	8, {0, -1536, -5312, -6592, -3884, -1136, -141, 0, 1},	},
	{5, {6, 6, 6, 2, 2},	8, {0, 0, -3072, -5632, -3840, -1184, -148, 0, 1},	},

	{6, {2, 2, 2, 2, 2, 2},	6, {-320, -768, -720, -320, -60, 0, 1},	},
	{6, {3, 2, 2, 2, 2, 2},	7, {512, 1680, 2208, 1480, 520, 81, 0, -1},	},
	{6, {5, 2, 2, 2, 2, 2},	7, {768, 2320, 2848, 1800, 600, 89, 0, -1},	},
	{6, {6, 2, 2, 2, 2, 2},	7, {512, 1920, 2688, 1840, 640, 96, 0, -1},	},
	{6, {3, 3, 2, 2, 2, 2},	8, {-816, -3392, -5816, -5312, -2767, -800, -106, 0, 1},	},
	{6, {5, 3, 2, 2, 2, 2},	8, {-1200, -4672, -7544, -6528, -3239, -896, -114, 0, 1},	},
	{6, {6, 3, 2, 2, 2, 2},	8, {-768, -3584, -6608, -6272, -3304, -944, -121, 0, 1},	},
	{6, {5, 5, 2, 2, 2, 2},	8, {-1584, -5952, -9272, -7744, -3711, -992, -122, 0, 1},	},
	{6, {6, 5, 2, 2, 2, 2},	8, {-768, -4096, -7760, -7296, -3752, -1040, -129, 0, 1},	},
	{6, {6, 6, 2, 2, 2, 2},	8, {0, -2048, -5888, -6656, -3760, -1088, -136, 0, 1},	},

	{7, {2, 2, 2, 2, 2, 2, 2},	7, {768, 2240, 2688, 1680, 560, 84, 0, -1},	},
	{7, {3, 2, 2, 2, 2, 2, 2},	8, {-1216, -4608, -7280, -6208, -3060, -848, -109, 0, 1},	},
	{7, {5, 2, 2, 2, 2, 2, 2},	8, {-1728, -6144, -9200, -7488, -3540, -944, -117, 0, 1},	},
	{7, {6, 2, 2, 2, 2, 2, 2},	8, {-1024, -4608, -8000, -7168, -3600, -992, -124, 0, 1},	},

	{8, {2, 2, 2, 2, 2, 2, 2, 2},	8, {-1792, -6144, -8960, -7168, -3360, -896, -112, 0, 1},	},

	/*
	 * mesh errors
	 */
	{2, {6, 6},                     4, {-192, -256, -80, 0, 1}, },

	{-1, {0,}, 0, {0, },					},
};

/*
 * per fabric mesh info
 */
typedef struct _mesh {
	int num_class;			/* number of switch classes */
	int *class_type;		/* index of first switch found for each class */
	int *class_count;		/* population of each class */
	int dimension;			/* mesh dimension */
	int *size;			/* an array to hold size of mesh */
	int dim_order[MAX_DIMENSION];
} mesh_t;

typedef struct sort_ctx {
	lash_t *p_lash;
	mesh_t *mesh;
} sort_ctx_t;

typedef struct comp {
	int index;
	sort_ctx_t ctx;
} comp_t;

/*
 * poly_alloc
 *
 * allocate a polynomial of degree n
 */
static int *poly_alloc(lash_t *p_lash, int n)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int *p;

	if (!(p = calloc(n+1, sizeof(int))))
		OSM_LOG(p_log, OSM_LOG_ERROR,
			"Failed allocating poly - out of memory\n");

	return p;
}

/*
 * print a polynomial
 */
static char *poly_print(int n, int *coeff)
{
	static char str[(MAX_DEGREE+1)*20];
	char *p = str;
	int i;
	int first = 1;
	int t;
	int sign;

	str[0] = 0;

	for (i = 0; i <= n; i++) {
		if (!coeff[i])
			continue;

		if (coeff[i] < 0) {
			sign = 1;
			t = -coeff[i];
		} else {
			sign = 0;
			t = coeff[i];
		}

		p += sprintf(p, "%s", sign? "-" : (first? "" : "+"));
		first = 0;

		if (t != 1 || i == 0)
			p += sprintf(p, "%d", t);

		if (i)
			p += sprintf(p, "x");
		if (i > 1)
			p += sprintf(p, "^%d", i);
	}

	return str;
}

/*
 * poly_diff
 *
 * return a nonzero value if polynomials differ else 0
 */
static int poly_diff(unsigned int n, const int *p, switch_t *s)
{
	if (s->node->num_links != n)
		return 1;

	return memcmp(p, s->node->poly, n*sizeof(int));
}

/*
 * m_free
 *
 * free a square matrix of rank l
 */
static void m_free(int **m, int l)
{
	int i;

	if (m) {
		for (i = 0; i < l; i++) {
			if (m[i])
				free(m[i]);
		}
		free(m);
	}
}

/*
 * m_alloc
 *
 * allocate a square matrix of rank l
 */
static int **m_alloc(lash_t *p_lash, int l)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int i;
	int **m = NULL;

	do {
		if (!(m = calloc(l, sizeof(int *))))
			break;

		for (i = 0; i < l; i++) {
			if (!(m[i] = calloc(l, sizeof(int))))
				break;
		}
		if (i != l)
			break;

		return m;
	} while (0);

	OSM_LOG(p_log, OSM_LOG_ERROR,
		"Failed allocating matrix - out of memory\n");

	m_free(m, l);
	return NULL;
}

/*
 * pm_free
 *
 * free a square matrix of rank l of polynomials
 */
static void pm_free(int ***m, int l)
{
	int i, j;

	if (m) {
		for (i = 0; i < l; i++) {
			if (m[i]) {
				for (j = 0; j < l; j++) {
					if (m[i][j])
						free(m[i][j]);
				}
				free(m[i]);
			}
		}
		free(m);
	}
}

/*
 * pm_alloc
 *
 * allocate a square matrix of rank l of polynomials of degree n
 */
static int ***pm_alloc(lash_t *p_lash, int l, int n)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int i, j;
	int ***m = NULL;

	do {
		if (!(m = calloc(l, sizeof(int **))))
			break;

		for (i = 0; i < l; i++) {
			if (!(m[i] = calloc(l, sizeof(int *))))
				break;

			for (j = 0; j < l; j++) {
				if (!(m[i][j] = calloc(n+1, sizeof(int))))
					break;
			}
			if (j != l)
				break;
		}
		if (i != l)
			break;

		return m;
	} while (0);

	OSM_LOG(p_log, OSM_LOG_ERROR,
		"Failed allocating matrix - out of memory\n");

	pm_free(m, l);
	return NULL;
}

static int determinant(lash_t *p_lash, int n, int rank, int ***m, int *p);

/*
 * sub_determinant
 *
 * compute the determinant of a submatrix of matrix of rank l of polynomials of degree n
 * with row and col removed in poly. caller must free poly
 */
static int sub_determinant(lash_t *p_lash, int n, int l, int row, int col,
			   int ***matrix, int **poly)
{
	int ret = -1;
	int ***m = NULL;
	int *p = NULL;
	int i, j, k, x, y;
	int rank = l - 1;

	do {
		if (!(p = poly_alloc(p_lash, n))) {
			break;
		}

		if (rank <= 0) {
			p[0] = 1;
			ret = 0;
			break;
		}

		if (!(m = pm_alloc(p_lash, rank, n))) {
			free(p);
			p = NULL;
			break;
		}

		x = 0;
		for (i = 0; i < l; i++) {
			if (i == row)
				continue;

			y = 0;
			for (j = 0; j < l; j++) {
				if (j == col)
					continue;

				for (k = 0; k <= n; k++)
					m[x][y][k] = matrix[i][j][k];

				y++;
			}
			x++;
		}

		if (determinant(p_lash, n, rank, m, p)) {
			free(p);
			p = NULL;
			break;
		}

		ret = 0;
	} while (0);

	pm_free(m, rank);
	*poly = p;
	return ret;
}

/*
 * determinant
 *
 * compute the determinant of matrix m of rank of polynomials of degree deg
 * and add the result to polynomial p allocated by caller
 */
static int determinant(lash_t *p_lash, int deg, int rank, int ***m, int *p)
{
	int i, j, k;
	int *q;
	int sign = 1;

	/*
	 * handle simple case of 1x1 matrix
	 */
	if (rank == 1) {
		for (i = 0; i <= deg; i++)
			p[i] += m[0][0][i];
	}

	/*
	 * handle simple case of 2x2 matrix
	 */
	else if (rank == 2) {
		for (i = 0; i <= deg; i++) {
			if (m[0][0][i] == 0)
				continue;

			for (j = 0; j <= deg; j++) {
				if (m[1][1][j] == 0)
					continue;

				p[i+j] += m[0][0][i]*m[1][1][j];
			}
		}

		for (i = 0; i <= deg; i++) {
			if (m[0][1][i] == 0)
				continue;

			for (j = 0; j <= deg; j++) {
				if (m[1][0][j] == 0)
					continue;

				p[i+j] -= m[0][1][i]*m[1][0][j];
			}
		}
	}

	/*
	 * handle the general case
	 */
	else {
		for (i = 0; i < rank; i++) {
			if (sub_determinant(p_lash, deg, rank, 0, i, m, &q))
				return -1;

			for (j = 0; j <= deg; j++) {
				if (m[0][i][j] == 0)
					continue;

				for (k = 0; k <= deg; k++) {
					if (q[k] == 0)
						continue;

					p[j+k] += sign*m[0][i][j]*q[k];
				}
			}

			free(q);
			sign = -sign;
		}
	}

	return 0;
}

/*
 * char_poly
 *
 * compute the characteristic polynomial of matrix of rank
 * by computing the determinant of m-x*I and return in poly
 * as an array. caller must free poly
 */
static int char_poly(lash_t *p_lash, int rank, int **matrix, int **poly)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int ret = -1;
	int i, j;
	int ***m = NULL;
	int *p = NULL;
	int deg = rank;

	OSM_LOG_ENTER(p_log);

	do {
		if (!matrix)
			break;

		if (!(p = poly_alloc(p_lash, deg)))
			break;

		if (!(m = pm_alloc(p_lash, rank, deg))) {
			free(p);
			p = NULL;
			break;
		}

		for (i = 0; i < rank; i++) {
			for (j = 0; j < rank; j++) {
				m[i][j][0] = matrix[i][j];
			}
			m[i][i][1] = -1;
		}

		if (determinant(p_lash, deg, rank, m, p)) {
			free(p);
			p = NULL;
			break;
		}

		ret = 0;
	} while (0);

	pm_free(m, rank);
	*poly = p;

	OSM_LOG_EXIT(p_log);
	return ret;
}

/*
 * get_switch_metric
 *
 * compute the matrix of minimum distances between each of
 * the adjacent switch nodes to sw along paths
 * that do not go through sw. do calculation by
 * relaxation method
 * allocate space for the matrix and save in node_t structure
 */
static int get_switch_metric(lash_t *p_lash, int sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int ret = -1;
	unsigned int i, j, change;
	int sw1, sw2, sw3;
	switch_t *s = p_lash->switches[sw];
	switch_t *s1, *s2, *s3;
	int **m;
	mesh_node_t *node = s->node;
	unsigned int num_links = node->num_links;

	OSM_LOG_ENTER(p_log);

	do {
		if (!(m = m_alloc(p_lash, num_links)))
			break;

		for (i = 0; i < num_links; i++) {
			sw1 = node->links[i]->switch_id;
			s1 = p_lash->switches[sw1];

			/* make all distances big except s1 to itself */
			for (sw2 = 0; sw2 < p_lash->num_switches; sw2++)
				p_lash->switches[sw2]->node->temp = LARGE;

			s1->node->temp = 0;

			do {
				change = 0;

				for (sw2 = 0; sw2 < p_lash->num_switches; sw2++) {
					s2 = p_lash->switches[sw2];
					if (s2->node->temp == LARGE)
						continue;
					for (j = 0; j < s2->node->num_links; j++) {
						sw3 = s2->node->links[j]->switch_id;
						s3 = p_lash->switches[sw3];

						if (sw3 == sw)
							continue;

						if ((s2->node->temp + 1) < s3->node->temp) {
							s3->node->temp = s2->node->temp + 1;
							change++;
						}
					}
				}
			} while (change);

			for (j = 0; j < num_links; j++) {
				sw2 = node->links[j]->switch_id;
				s2 = p_lash->switches[sw2];
				m[i][j] = s2->node->temp;
			}
		}

		if (char_poly(p_lash, num_links, m, &node->poly)) {
			m_free(m, num_links);
			m = NULL;
			break;
		}

		ret = 0;
	} while (0);

	node->matrix = m;

	OSM_LOG_EXIT(p_log);
	return ret;
}

/*
 * classify_switch
 *
 * add switch to histogram of switch types
 * we keep a reference to the first switch
 * found of each type as an exemplar
 */
static void classify_switch(lash_t *p_lash, mesh_t *mesh, int sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int i;
	switch_t *s = p_lash->switches[sw];
	switch_t *s1;

	OSM_LOG_ENTER(p_log);

	if (!s->node->poly)
		goto done;

	for (i = 0; i < mesh->num_class; i++) {
		s1 = p_lash->switches[mesh->class_type[i]];

		if (poly_diff(s->node->num_links, s->node->poly, s1))
			continue;

		mesh->class_count[i]++;
		goto done;
	}

	mesh->class_type[mesh->num_class] = sw;
	mesh->class_count[mesh->num_class] = 1;
	mesh->num_class++;

done:
	OSM_LOG_EXIT(p_log);
}

/*
 * classify_mesh_type
 *
 * try to look up node polynomial in table
 */
static void classify_mesh_type(lash_t *p_lash, int sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int i;
	switch_t *s = p_lash->switches[sw];
	const struct mesh_info *t;

	OSM_LOG_ENTER(p_log);

	if (!s->node->poly)
		goto done;

	for (i = 1; (t = &mesh_info[i])->dimension != -1; i++) {
		if (poly_diff(t->degree, t->poly, s))
			continue;

		s->node->type = i;
		s->node->dimension = t->dimension;
		OSM_LOG_EXIT(p_log);
		return;
	}

done:
	s->node->type = 0;
	OSM_LOG_EXIT(p_log);
	return;
}

/*
 * remove_edges
 *
 * remove type from nodes that have fewer links
 * than adjacent nodes
 */
static void remove_edges(lash_t *p_lash)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int sw;
	mesh_node_t *n, *nn;
	unsigned i;

	OSM_LOG_ENTER(p_log);

	for (sw = 0; sw < p_lash->num_switches; sw++) {
		n = p_lash->switches[sw]->node;
		if (!n->type)
			continue;

		for (i = 0; i < n->num_links; i++) {
			nn = p_lash->switches[n->links[i]->switch_id]->node;

			if (nn->num_links > n->num_links) {
				OSM_LOG(p_log, OSM_LOG_DEBUG,
					"removed edge switch %s\n",
					p_lash->switches[sw]->p_sw->p_node->print_desc);
				n->type = -1;
				break;
			}
		}
	}

	OSM_LOG_EXIT(p_log);
}

/*
 * get_local_geometry
 *
 * analyze the local geometry around each switch
 */
static int get_local_geometry(lash_t *p_lash, mesh_t *mesh)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int sw;
	int status = 0;

	OSM_LOG_ENTER(p_log);

	for (sw = 0; sw < p_lash->num_switches; sw++) {
		/*
		 * skip switches with more links than MAX_DEGREE
		 * since they will never match a known case
		 */
		if (p_lash->switches[sw]->node->num_links > MAX_DEGREE)
			continue;

		if (get_switch_metric(p_lash, sw)) {
			status = -1;
			goto Exit;
		}
		classify_mesh_type(p_lash, sw);
	}

	remove_edges(p_lash);

	for (sw = 0; sw < p_lash->num_switches; sw++) {
		if (p_lash->switches[sw]->node->type < 0)
			continue;
		classify_switch(p_lash, mesh, sw);
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

static void print_axis(lash_t *p_lash, char *p, int sw, int port)
{
	mesh_node_t *node = p_lash->switches[sw]->node;
	char *name = p_lash->switches[sw]->p_sw->p_node->print_desc;
	int c = node->axes[port];

	p += sprintf(p, "%s[%d] = ", name, port);
	if (c)
		p += sprintf(p, "%s%c -> ", ((c - 1) & 1) ? "-" : "+", 'X' + (c - 1)/2);
	else
		p += sprintf(p, "N/A -> ");
	p += sprintf(p, "%s\n",
		     p_lash->switches[node->links[port]->switch_id]->p_sw->p_node->print_desc);
}

/*
 * seed_axes
 *
 * assign axes to the links of the seed switch
 * assumes switch is of type cartesian mesh
 * axes are numbered 1 to n i.e. +x => 1 -x => 2 etc.
 * this assumes that if all distances are 2 that
 * an axis has only 2 nodes so +A and -A collapse to +A
 */
static void seed_axes(lash_t *p_lash, int sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	mesh_node_t *node = p_lash->switches[sw]->node;
	int n = node->num_links;
	int i, j, c;

	OSM_LOG_ENTER(p_log);

	if (!node->matrix || !node->dimension)
		goto done;

	for (c = 1; c <= 2*node->dimension; c++) {
		/*
		 * find the next unassigned axis
		 */
		for (i = 0; i < n; i++) {
			if (!node->axes[i])
				break;
		}

		node->axes[i] = c++;

		/*
		 * find the matching opposite direction
		 */
		for (j = 0; j < n; j++) {
			if (node->axes[j] || j == i)
				continue;

			if (node->matrix[i][j] != 2)
				break;
		}

		if (j != n) {
			node->axes[j] = c;
		}
	}

	if (OSM_LOG_IS_ACTIVE_V2(p_log, OSM_LOG_DEBUG)) {
		char buf[256], *p;

		for (i = 0; i < n; i++) {
			p = buf;
			print_axis(p_lash, p, sw, i);
			OSM_LOG(p_log, OSM_LOG_DEBUG, "%s", buf);
		}
	}

done:
	OSM_LOG_EXIT(p_log);
}

/*
 * opposite
 *
 * compute the opposite of axis for switch
 */
static inline int opposite(switch_t *s, int axis)
{
	unsigned i, j;
	int negaxis = 1 + (1 ^ (axis - 1));

	if (!s->node->matrix)
		return 0;

	for (i = 0; i < s->node->num_links; i++) {
		if (s->node->axes[i] == axis) {
			for (j = 0; j < s->node->num_links; j++) {
				if (j == i)
					continue;
				if (s->node->matrix[i][j] != 2)
					return negaxis;
			}

			return axis;
		}
	}

	return 0;
}

/*
 * make_geometry
 *
 * induce a geometry on the switches
 */
static void make_geometry(lash_t *p_lash, int sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int num_switches = p_lash->num_switches;
	int sw1, sw2;
	switch_t *s, *s1, *s2, *seed;
	unsigned int i, j, k, l, n, m;
	unsigned int change;

	OSM_LOG_ENTER(p_log);

	s = p_lash->switches[sw];

	if (!s->node->matrix)
		goto done;

	/*
	 * assign axes to seed switch
	 */
	seed_axes(p_lash, sw);
	seed = p_lash->switches[sw];

	/*
	 * induce axes in other switches until
	 * there is no more change
	 */
	do {
		change = 0;

		/* phase 1 opposites */
		for (sw1 = 0; sw1 < num_switches; sw1++) {
			s1 = p_lash->switches[sw1];
			n = s1->node->num_links;

			/*
			 * ignore chain fragments
			 */
			if (n < seed->node->num_links && n <= 2)
				continue;

			/*
			 * only process 'mesh' switches
			 */
			if (!s1->node->matrix)
				continue;

			for (i = 0; i < n; i++) {
				if (!s1->node->axes[i])
					continue;

				/*
				 * can't tell across if more than one
				 * likely looking link
				 */
				m = 0;
				for (j = 0; j < n; j++) {
					if (j == i)
						continue;

					if (s1->node->matrix[i][j] != 2)
						m++;
				}

				if (m != 1) {
					continue;
				}

				for (j = 0; j < n; j++) {
					if (j == i)
						continue;

					/* Rule out opposite nodes when distance greater than 4 */
					if (s1->node->matrix[i][j] != 2 &&
					    s1->node->matrix[i][j] <= 4) {
						if (s1->node->axes[j]) {
							if (s1->node->axes[j] != opposite(seed, s1->node->axes[i])) {
								OSM_LOG(p_log, OSM_LOG_DEBUG,
									"phase 1 mismatch\n");
							}
						} else {
							s1->node->axes[j] = opposite(seed, s1->node->axes[i]);
							change++;
						}
					}
				}
			}
		}

		/* phase 2 switch to switch */
		for (sw1 = 0; sw1 < num_switches; sw1++) {
			s1 = p_lash->switches[sw1];
			n = s1->node->num_links;

			if (!s1->node->matrix)
				continue;

			for (i = 0; i < n; i++) {
				int l2 = s1->node->links[i]->link_id;

				if (!s1->node->axes[i])
					continue;

				if (l2 == -1) {
					OSM_LOG(p_log, OSM_LOG_DEBUG,
						"no reverse link\n");
					continue;
				}

				sw2 = s1->node->links[i]->switch_id;
				s2 = p_lash->switches[sw2];

				if (!s2->node->matrix)
					continue;

				if (!s2->node->axes[l2]) {
					/*
					 * set axis to opposite of s1->axes[i]
					 */
					s2->node->axes[l2] = opposite(seed, s1->node->axes[i]);
					change++;
				} else {
					if (s2->node->axes[l2] != opposite(seed, s1->node->axes[i])) {
						OSM_LOG(p_log, OSM_LOG_DEBUG,
							"phase 2 mismatch\n");
					}
				}
			}
		}

		/* Phase 3 corners */
		for (sw1 = 0; sw1 < num_switches; sw1++) {
			s = p_lash->switches[sw1];
			n = s->node->num_links;

			if (!s->node->matrix)
				continue;

			for (i = 0; i < n; i++) {
				if (!s->node->axes[i])
					continue;

				for (j = 0; j < n; j++) {
					if (i == j || !s->node->axes[j] || s->node->matrix[i][j] != 2)
						continue;

					s1 = p_lash->switches[s->node->links[i]->switch_id];
					s2 = p_lash->switches[s->node->links[j]->switch_id];

					/*
					 * find switch (other than s1) that neighbors i and j
					 * have in common
					 */
					for (k = 0; k < s1->node->num_links; k++) {
						if (s1->node->links[k]->switch_id == sw1)
							continue;

						for (l = 0; l < s2->node->num_links; l++) {
							if (s2->node->links[l]->switch_id == sw1)
								continue;

							if (s1->node->links[k]->switch_id == s2->node->links[l]->switch_id) {
								if (s1->node->axes[k]) {
									if (s1->node->axes[k] != s->node->axes[j]) {
										OSM_LOG(p_log, OSM_LOG_DEBUG, "phase 3 mismatch\n");
									}
								} else {
									s1->node->axes[k] = s->node->axes[j];
									change++;
								}

								if (s2->node->axes[l]) {
									if (s2->node->axes[l] != s->node->axes[i]) {
										OSM_LOG(p_log, OSM_LOG_DEBUG, "phase 3 mismatch\n");
									}
								} else {
									s2->node->axes[l] = s->node->axes[i];
									change++;
								}
								goto next_j;
							}
						}
					}
next_j:
					;
				}
			}
		}
	} while (change);

done:
	OSM_LOG_EXIT(p_log);
}

/*
 * return |a| < |b|
 */
static inline int ltmag(int a, int b)
{
	int a1 = (a >= 0)? a : -a;
	int b1 = (b >= 0)? b : -b;

	return (a1 < b1) || (a1 == b1 && a > b);
}

/*
 * reorder_node_links
 *
 * reorder the links out of a switch in sign/dimension order
 */
static int reorder_node_links(lash_t *p_lash, mesh_t *mesh, int sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	switch_t *s = p_lash->switches[sw];
	mesh_node_t *node = s->node;
	int n = node->num_links;
	link_t **links;
	int *axes;
	int i, j, k, l;
	int c;
	int next = 0;
	int dimension = mesh->dimension;

	if (!(links = calloc(n, sizeof(link_t *)))) {
		OSM_LOG(p_log, OSM_LOG_ERROR,
			"Failed allocating links array - out of memory\n");
		return -1;
	}

	if (!(axes = calloc(n, sizeof(int)))) {
		free(links);
		OSM_LOG(p_log, OSM_LOG_ERROR,
			"Failed allocating axes array - out of memory\n");
		return -1;
	}

	/*
	 * find the links with axes
	 */
	for (i = 0; i < dimension; i++) {
		j = mesh->dim_order[i];
		for (k = 1; k <= 2; k++) {
			c = 2*j + k;

			if (node->coord[j] > 0)
				c = opposite(s, c);

			for (l = 0; l < n; l++) {
				if (!node->links[l])
					continue;
				if (node->axes[l] == c) {
					links[next] = node->links[l];
					axes[next] = node->axes[l];
					node->links[l] = NULL;
					next++;
				}
			}
		}
	}

	/*
	 * get the rest
	 */
	for (i = 0; i < n; i++) {
		if (!node->links[i])
			continue;

		links[next] = node->links[i];
		axes[next] = node->axes[i];
		node->links[i] = NULL;
		next++;
	}

	for (i = 0; i < n; i++) {
		node->links[i] = links[i];
		node->axes[i] = axes[i];
	}

	free(links);
	free(axes);

	return 0;
}

/*
 * make_coord
 */
static int make_coord(lash_t *p_lash, mesh_t *mesh, int seed)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	unsigned int i, j, k;
	int sw;
	switch_t *s, *s1;
	unsigned int change;
	unsigned int dimension = mesh->dimension;
	int num_switches = p_lash->num_switches;
	int assigned_axes = 0, unassigned_axes = 0;

	OSM_LOG_ENTER(p_log);

	for (sw = 0; sw < num_switches; sw++) {
		s = p_lash->switches[sw];

		s->node->coord = calloc(dimension, sizeof(int));
		if (!s->node->coord) {
			OSM_LOG(p_log, OSM_LOG_ERROR,
				"Failed allocating coord - out of memory\n");
			OSM_LOG_EXIT(p_log);
			return -1;
		}

		for (i = 0; i < dimension; i++)
			s->node->coord[i] = (sw == seed) ? 0 : LARGE;

		for (i = 0; i < s->node->num_links; i++)
			if (s->node->axes[i] == 0)
				unassigned_axes++;
			else
				assigned_axes++;
	}

	OSM_LOG(p_log, OSM_LOG_DEBUG, "%d/%d unassigned/assigned axes\n",
		unassigned_axes, assigned_axes);

	do {
		change = 0;

		for (sw = 0; sw < num_switches; sw++) {
			s = p_lash->switches[sw];

			if (s->node->coord[0] == LARGE)
				continue;

			for (j = 0; j < s->node->num_links; j++) {
				if (!s->node->axes[j])
					continue;

				s1 = p_lash->switches[s->node->links[j]->switch_id];

				for (k = 0; k < dimension; k++) {
					int coord = s->node->coord[k];
					unsigned axis = s->node->axes[j] - 1;

					if (k == axis/2)
						coord += (axis & 1)? -1 : +1;

					if (ltmag(coord, s1->node->coord[k])) {
						s1->node->coord[k] = coord;
						change++;
					}
				}
			}
		}
	} while (change);

	OSM_LOG_EXIT(p_log);
	return 0;
}

/*
 * measure geometry
 */
static int measure_geometry(lash_t *p_lash, mesh_t *mesh)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int i, j;
	int sw;
	switch_t *s;
	int dimension = mesh->dimension;
	int num_switches = p_lash->num_switches;
	int max[MAX_DIMENSION];
	int min[MAX_DIMENSION];
	int size[MAX_DIMENSION];
	int max_size;
	int max_index;

	OSM_LOG_ENTER(p_log);

	mesh->size = calloc(dimension, sizeof(int));
	if (!mesh->size) {
		OSM_LOG(p_log, OSM_LOG_ERROR,
			"Failed allocating size - out of memory\n");
		OSM_LOG_EXIT(p_log);
		return -1;
	}

	for (i = 0; i < dimension; i++) {
		max[i] = -LARGE;
		min[i] = LARGE;
	}

	for (sw = 0; sw < num_switches; sw++) {
		s = p_lash->switches[sw];

		for (i = 0; i < dimension; i++) {
			if (s->node->coord[i] == LARGE)
				continue;
			if (s->node->coord[i] > max[i])
				max[i] = s->node->coord[i];
			if (s->node->coord[i] < min[i])
				min[i] = s->node->coord[i];
		}
	}

	for (i = 0; i < dimension; i++)
		mesh->size[i] = size[i] = max[i] - min[i] + 1;

	/*
	 * find an order of dimensions that places largest
	 * sizes first since this seems to work best with LASH
	 */
	for (j = 0; j < dimension; j++) {
		max_size = -1;
		max_index = -1;

		for (i = 0; i < dimension; i++) {
			if (size[i] > max_size) {
				max_size = size[i];
				max_index = i;
			}
		}

		mesh->dim_order[j] = max_index;
		size[max_index] = -1;
	}

	OSM_LOG_EXIT(p_log);
	return 0;
}

/*
 * reorder links
 */
static int reorder_links(lash_t *p_lash, mesh_t *mesh)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int sw;
	int num_switches = p_lash->num_switches;

	OSM_LOG_ENTER(p_log);

	for (sw = 0; sw < num_switches; sw++) {
		if (reorder_node_links(p_lash, mesh, sw)) {
			OSM_LOG_EXIT(p_log);
			return -1;
		}
	}

	OSM_LOG_EXIT(p_log);
	return 0;
}

/*
 * compare two switches in a sort
 */
static int compare_switches(const void *p1, const void *p2)
{
	const comp_t *cp1 = p1, *cp2 = p2;
	const sort_ctx_t *ctx = &cp1->ctx;
	switch_t *s1 = ctx->p_lash->switches[cp1->index];
	switch_t *s2 = ctx->p_lash->switches[cp2->index];
	int i, j;
	int ret;

	for (i = 0; i < ctx->mesh->dimension; i++) {
		j = ctx->mesh->dim_order[i];
		ret = s1->node->coord[j] - s2->node->coord[j];
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * sort_switches - reorder switch array
 */
static void sort_switches(lash_t *p_lash, mesh_t *mesh)
{
	unsigned int i, j;
	unsigned int num_switches = p_lash->num_switches;
	comp_t *comp;
	int *reverse;
	switch_t *s;
	switch_t **switches;

	comp = malloc(num_switches * sizeof(comp_t));
	reverse = malloc(num_switches * sizeof(int));
	switches = malloc(num_switches * sizeof(switch_t *));
	if (!comp || !reverse || !switches) {
		OSM_LOG(&p_lash->p_osm->log, OSM_LOG_ERROR,
			"Failed memory allocation - switches not sorted!\n");
		goto Exit;
	}

	for (i = 0; i < num_switches; i++) {
		comp[i].index = i;
		comp[i].ctx.mesh = mesh;
		comp[i].ctx.p_lash = p_lash;
	}

	qsort(comp, num_switches, sizeof(comp_t), compare_switches);

	for (i = 0; i < num_switches; i++)
		reverse[comp[i].index] = i;

	for (i = 0; i < num_switches; i++) {
		s = p_lash->switches[comp[i].index];
		switches[i] = s;
		s->id = i;
		for (j = 0; j < s->node->num_links; j++)
			s->node->links[j]->switch_id =
				reverse[s->node->links[j]->switch_id];
	}

	for (i = 0; i < num_switches; i++)
		p_lash->switches[i] = switches[i];

Exit:
	if (switches)
		free(switches);
	if (comp)
		free(comp);
	if (reverse)
		free(reverse);
}

/*
 * osm_mesh_delete - free per mesh resources
 */
static void mesh_delete(mesh_t *mesh)
{
	if (mesh) {
		if (mesh->class_type)
			free(mesh->class_type);

		if (mesh->class_count)
			free(mesh->class_count);

		if (mesh->size)
			free(mesh->size);

		free(mesh);
	}
}

/*
 * osm_mesh_create - allocate per mesh resources
 */
static mesh_t *mesh_create(lash_t *p_lash)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	mesh_t *mesh;

	if(!(mesh = calloc(1, sizeof(mesh_t))))
		goto err;

	if (!(mesh->class_type = calloc(p_lash->num_switches, sizeof(int))))
		goto err;

	if (!(mesh->class_count = calloc(p_lash->num_switches, sizeof(int))))
		goto err;

	return mesh;

err:
	mesh_delete(mesh);
	OSM_LOG(p_log, OSM_LOG_ERROR,
		"Failed allocating mesh - out of memory\n");
	return NULL;
}

/*
 * osm_mesh_node_delete - cleanup per switch resources
 */
void osm_mesh_node_delete(lash_t *p_lash, switch_t *sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	unsigned i;
	mesh_node_t *node = sw->node;
	unsigned num_ports = sw->p_sw->num_ports;

	OSM_LOG_ENTER(p_log);

	if (node) {
		for (i = 0; i < num_ports; i++)
			if (node->links[i])
				free(node->links[i]);

		if (node->poly)
			free(node->poly);

		if (node->matrix) {
			for (i = 0; i < node->num_links; i++) {
				if (node->matrix[i])
					free(node->matrix[i]);
			}
			free(node->matrix);
		}

		if (node->axes)
			free(node->axes);

		if (node->coord)
			free(node->coord);

		free(node);

		sw->node = NULL;
	}

	OSM_LOG_EXIT(p_log);
}

/*
 * osm_mesh_node_create - allocate per switch resources
 */
int osm_mesh_node_create(lash_t *p_lash, switch_t *sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	unsigned i;
	mesh_node_t *node;
	unsigned num_ports = sw->p_sw->num_ports;

	OSM_LOG_ENTER(p_log);

	if (!(node = sw->node = calloc(1, sizeof(mesh_node_t) + num_ports * sizeof(link_t *))))
		goto err;

	for (i = 0; i < num_ports; i++)
		if (!(node->links[i] = calloc(1, sizeof(link_t) + num_ports * sizeof(int))))
			goto err;

	if (!(node->axes = calloc(num_ports, sizeof(int))))
		goto err;

	for (i = 0; i < num_ports; i++) {
		node->links[i]->switch_id = NONE;
	}

	OSM_LOG_EXIT(p_log);
	return 0;

err:
	osm_mesh_node_delete(p_lash, sw);
	OSM_LOG(p_log, OSM_LOG_ERROR,
		"Failed allocating mesh node - out of memory\n");
	OSM_LOG_EXIT(p_log);
	return -1;
}

static void dump_mesh(lash_t *p_lash)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int sw;
	int num_switches = p_lash->num_switches;
	int dimension;
	int i, j, k, n;
	switch_t *s, *s2;
	char buf[256];

	OSM_LOG_ENTER(p_log);

	for (sw = 0; sw < num_switches; sw++) {
		s = p_lash->switches[sw];
		dimension = s->node->dimension;
		n = sprintf(buf, "[");
		for (i = 0; i < dimension; i++) {
			n += snprintf(buf + n, sizeof(buf) - n,
				      "%2d", s->node->coord[i]);
			if (n > sizeof(buf))
				n = sizeof(buf);
			if (i != dimension - 1) {
				n += snprintf(buf + n, sizeof(buf) - n, "%s", ",");
				if (n > sizeof(buf))
					n = sizeof(buf);
			}
		}
		n += snprintf(buf + n, sizeof(buf) - n, "]");
		if (n > sizeof(buf))
			n = sizeof(buf);
		for (j = 0; j < s->node->num_links; j++) {
			s2 = p_lash->switches[s->node->links[j]->switch_id];
			n += snprintf(buf + n, sizeof(buf) - n, " [%d]->[", j);
			if (n > sizeof(buf))
				n = sizeof(buf);
			for (k = 0; k < dimension; k++) {
				n += snprintf(buf + n, sizeof(buf) - n, "%2d",
					      s2->node->coord[k]);
				if (n > sizeof(buf))
					n = sizeof(buf);
				if (k != dimension - 1) {
					n += snprintf(buf + n, sizeof(buf) - n,
						      ",");
					if (n > sizeof(buf))
						n = sizeof(buf);
				}
			}
			n += snprintf(buf + n, sizeof(buf) - n, "]");
			if (n > sizeof(buf))
				n = sizeof(buf);
		}
		OSM_LOG(p_log, OSM_LOG_DEBUG, "%s\n", buf);
	}

	OSM_LOG_EXIT(p_log);
}

/*
 * osm_do_mesh_analysis
 */
int osm_do_mesh_analysis(lash_t *p_lash)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	mesh_t *mesh;
	int max_class_num = 0;
	int max_class_type = -1;
	int i;
	switch_t *s;
	char buf[256], *p;

	OSM_LOG_ENTER(p_log);

	mesh = mesh_create(p_lash);
	if (!mesh)
		goto err;

	if (get_local_geometry(p_lash, mesh))
		goto err;

	if (mesh->num_class == 0) {
		OSM_LOG(p_log, OSM_LOG_INFO,
			"found no likely mesh nodes - done\n");
		goto done;
	}

	/*
	 * find dominant switch class
	 */
	OSM_LOG(p_log, OSM_LOG_INFO, "found %d node class%s\n",
		mesh->num_class, (mesh->num_class == 1) ? "" : "es");
	for (i = 0; i < mesh->num_class; i++) {
		OSM_LOG(p_log, OSM_LOG_INFO,
			"class[%d] has %d members with type = %d\n",
			i, mesh->class_count[i],
			p_lash->switches[mesh->class_type[i]]->node->type);
		if (mesh->class_count[i] > max_class_num) {
			max_class_num = mesh->class_count[i];
			max_class_type = mesh->class_type[i];
		}
	}

	s = p_lash->switches[max_class_type];

	p = buf;
	p += sprintf(p, "%snode shape is ",
		    (mesh->num_class == 1) ? "" : "most common ");

	if (s->node->type) {
		const struct mesh_info *t = &mesh_info[s->node->type];

		for (i = 0; i < t->dimension; i++) {
			p += sprintf(p, "%s%d%s", i? " x " : "", t->size[i],
				(t->size[i] == 6)? "+" : "");
		}
		p += sprintf(p, " mesh\n");

		mesh->dimension = t->dimension;
	} else {
		p += sprintf(p, "unknown geometry\n");
	}

	OSM_LOG(p_log, OSM_LOG_INFO, "%s", buf);

	OSM_LOG(p_log, OSM_LOG_INFO, "poly = %s\n",
		poly_print(s->node->num_links, s->node->poly));

	if (s->node->type) {
		make_geometry(p_lash, max_class_type);

		if (make_coord(p_lash, mesh, max_class_type))
			goto err;

		if (measure_geometry(p_lash, mesh))
			goto err;

		if (reorder_links(p_lash, mesh))
			goto err;

		sort_switches(p_lash, mesh);

		p = buf;
		p += sprintf(p, "found ");
		for (i = 0; i < mesh->dimension; i++)
			p += sprintf(p, "%s%d", i? " x " : "", mesh->size[i]);
		p += sprintf(p, " mesh\n");

		OSM_LOG(p_log, OSM_LOG_INFO, "%s", buf);
	}

	if (OSM_LOG_IS_ACTIVE_V2(p_log, OSM_LOG_DEBUG))
		dump_mesh(p_lash);

done:
	mesh_delete(mesh);
	OSM_LOG_EXIT(p_log);
	return 0;

err:
	mesh_delete(mesh);
	OSM_LOG_EXIT(p_log);
	return -1;
}
