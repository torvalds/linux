.. SPDX-License-Identifier: GPL-2.0

============================================
(Como evitar) Deixar as ioctls malfeitas
============================================

De: https://blog.ffwll.ch/2013/11/botching-up-ioctls.html

Por: Daniel Vetter, Copyright © 2013 Intel Corporation

Uma percepção clara que os hackers de gráficos do kernel tiveram nos últimos
anos é que tentar criar uma interface unificada para gerenciar as unidades de
execução e a memória em GPUs completamente diferentes é um esforço inútil.
Portanto, hoje em dia, cada driver tem seu próprio conjunto de ioctls para
alocar memória e enviar trabalho para a GPU. O que é bom, já que não há mais a
insanidade na forma de interfaces falsamente genéricas, mas que na verdade só
são usadas uma vez. No entanto, a desvantagem clara é que há muito mais
potencial para estragar as coisas.

Para evitar repetir todos os mesmos erros novamente, escrevi algumas das lições
aprendidas enquanto fazia um trabalho malfeito para o driver drm/i915. A maioria
delas aborda apenas tecnicalidades e não os problemas macro (big-picture), como
deveria ser exatamente a aparência da ioctl de envio de comando. Aprender essas
lições é provavelmente algo que cada driver de GPU tem que fazer por conta
própria.


Pré-requisitos
--------------

Primeiro, os pré-requisitos. Sem estes você já falhou, porque precisará
adicionar uma camada de compatibilidade de 32 bits (compat layer):

 * Use apenas inteiros de tamanho fixo. Para evitar conflitos com typedefs no
   espaço de usuário (userspace), o kernel possui tipos especiais como __u32 e
   __s64. Use-os.

 * Alinhe tudo ao tamanho natural e use preenchimento (padding) explícito.
   Plataformas de 32 bits não alinham necessariamente valores de 64 bits a
   limites (boundaries) de 64 bits, mas plataformas de 64 bits o fazem. Portanto,
   sempre precisamos de padding para o tamanho natural para acertar isso.

 * Preencha a struct inteira para um múltiplo de 64 bits se a estrutura contiver
   tipos de 64 bits -- caso contrário, o tamanho da estrutura diferirá entre
   32 bits e 64 bits. Ter um tamanho de estrutura diferente prejudica ao passar
   matrizes (arrays) de estruturas para o kernel, ou se o kernel verificar o
   tamanho da estrutura, o que o core do drm, por exemplo, faz.

 * Ponteiros são __u64, convertidos de/para um uintptr_t no lado do espaço de
   usuário e de/para um void __user * no kernel. Tente de verdade não atrasar
   essa conversão ou, pior ainda, manipular o __u64 bruto pelo seu código, pois
   isso diminui a verificação que ferramentas como o sparse podem fornecer. A
   macro u64_to_user_ptr pode ser usada no kernel para evitar avisos sobre
   inteiros e ponteiros de tamanhos diferentes.


Conceitos básicos
-----------------

Evitadas as alegrias de escrever uma camada de compatibilidade (compat layer),
podemos dar uma olhada nos deslizes básicos. Negligenciar estes pontos tornará a
compatibilidade retroativa e futura uma verdadeira dor de cabeça. E, como errar
na primeira tentativa é garantido, você certamente terá uma segunda iteração ou,
pelo menos, uma extensão para qualquer interface fornecida.

 * Tenha uma maneira clara para o espaço de usuário descobrir se a sua nova
   ioctl ou extensão de ioctl é suportada em um determinado kernel. Se você não
   puder confiar que os kernels antigos rejeitarão as novas flags/modos ou
   ioctls (já que fazer isso foi deixado de lado no passado), então você
   precisará de uma flag de recurso (feature flag) do driver ou de um número de
   revisão em algum lugar.

 * Tenha um plano para estender as ioctls com novas flags ou novos campos no
   final da estrutura. O core do drm verifica o tamanho passado para cada
   chamada de ioctl e preenche com zero (zero-extends) quaisquer divergências
   entre o kernel e o espaço de usuário. Isso ajuda, mas não é uma solução
   completa, já que um espaço de usuário mais novo em um kernel mais antigo não
   notará que os campos recém-adicionados no final estão sendo ignorados.
   Portanto, isso ainda exige novas flags de recurso do driver.

 * Verifique todos os campos e flags não utilizados, além de todo o preenchimento
   (padding), para garantir que estejam em 0, e rejeite a ioctl se esse não for
   o caso. Caso contrário, seu excelente plano para extensões futuras irá por
   água abaixo, pois alguém enviará uma struct de ioctl com lixo de pilha
   (stack garbage) aleatório nas partes ainda não utilizadas. O que, então,
   consolida na ABI que esses campos nunca poderão ser usados para nada além de
   lixo. Esta também é a razão pela qual você deve preencher explicitamente todas
   as estruturas, mesmo que nunca as use em uma matriz (array) -- o padding que
   o compilador possa inserir poderia conter lixo.

 * Tenha casos de teste simples para tudo o que foi mencionado acima.


Diversão com caminhos de erro (Error Paths)
-------------------------------------------

Hoje em dia, não temos mais nenhuma desculpa para que os drivers drm sejam pequenos
exploits de root disfarçados. Isso significa que precisamos tanto de uma
validação completa de entrada quanto de caminhos sólidos de tratamento de erros
-- as GPUs eventualmente vão parar de funcionar (die) nos casos mais bizarros
de qualquer maneira:

 * A ioctl deve verificar se há estouros de matriz (array overflows). Ela também
   precisa verificar estouros superiores/inferiores (over/underflows) e problemas
   de limitação (clamping) de valores inteiros em geral. O exemplo usual são os
   valores de posicionamento de sprite alimentados diretamente no hardware, onde
   o hardware possui apenas 12 bits ou algo assim. Funciona perfeitamente até que
   algum servidor de exibição bizarro não se preocupe em fazer o clamping por si
   mesmo e o cursor dê a volta (wrap around) na tela.

 * Tenha casos de teste simples para cada caso de falha de validação de entrada
   na sua ioctl. Verifique se o código de erro corresponde às suas expectativas.
   E, finalmente, certifique-se de testar apenas um único caminho de erro em
   cada subteste, enviando dados que, de outra forma, seriam perfeitamente
   válidos. Sem isso, uma verificação anterior já poderia rejeitar a ioctl e
   ofuscar (shadow) o caminho de código que você realmente deseja testar,
   ocultando bugs e regressões.

 * Torne todas as suas ioctls reiniciáveis (restartable). Primeiro, o X (X11)
   realmente ama sinais (signals) e, segundo, isso permitirá que você teste 90%
   de todos os caminhos de tratamento de erro apenas interrompendo sua suíte de
   testes principal constantemente com sinais. Graças ao amor do X por sinais,
   você obterá uma excelente cobertura de base de todos os seus caminhos de erro
   praticamente de graça para drivers de gráficos. Além disso, seja consistente
   na forma como você lida com a reinicialização de ioctls -- por exemplo, o drm
   possui um pequeno helper drmIoctl em sua biblioteca de espaço de usuário. O
   driver i915 estragou isso com a ioctl set_tiling; agora estamos presos para
   sempre com algumas semânticas arcanas tanto no kernel quanto no espaço de
   usuário.

 * Se você não puder tornar um determinado caminho de código reiniciável, torne
   uma tarefa travada pelo menos finalizável (killable). As GPUs simplesmente
   morrem, e seus usuários não vão gostar mais de você se você travar a máquina
   inteira deles (por meio de um processo do X impossível de matar). Se a
   recuperação de estado ainda for muito complicada, tenha um timeout ou uma
   rede de segurança de verificação de travamento (hangcheck) como um esforço de
   última hora (last-ditch) caso o hardware enlouqueça (gone bananas).

 * Tenha casos de teste para os cenários mais complexos (corner cases) no seu
   código de recuperação de erros -- é fácil demais criar um deadlock entre seu
   código de hangcheck e os processos que estão aguardando (waiters).


Tempo, Espera e a Perda de Prazos
---------------------------------

As GPUs fazem quase tudo de forma assíncrona, portanto, temos a necessidade de
cronometrar operações e aguardar pelas que estão pendentes. Esse é um negócio
realmente complicado; no momento, nenhuma das ioctls suportadas pelo drm/i915
acerta isso completamente, o que significa que ainda há toneladas de lições para
aprender aqui.

 * Use CLOCK_MONOTONIC como seu tempo de referência, sempre. É o que o alsa, o
   drm e o v4l usam por padrão hoje em dia. Mas informe ao espaço de usuário
   quais carimbos de data/hora (timestamps) são derivados de domínios de relógio
   diferentes, como o relógio principal do seu sistema (fornecido pelo kernel)
   ou algum contador de hardware independente em outro lugar. Os relógios vão
   divergir se você olhar de perto o suficiente, mas se as ferramentas de
   medição de desempenho tiverem essa informação, elas poderão ao menos compensar.
   Se o seu espaço de usuário puder obter os valores brutos de alguns relógios
   (por exemplo, por meio de instruções de amostragem de contador de desempenho
   no fluxo de comandos), considere expor esses também.

 * Use __s64 para segundos mais __u64 para nanossegundos para especificar o
   tempo. Não é a especificação de tempo mais conveniente, mas é praticamente o
   padrão.

 * Verifique se os valores de tempo de entrada estão normalizados e rejeite-os
   caso contrário. Note que a struct nativa do kernel, ktime, possui um inteiro
   sinalizado tanto para segundos quanto para nanossegundos, portanto, cuidado
   aqui.

 * Para timeouts, use tempos absolutos. Se você for um bom sujeito e tiver
   tornado a sua ioctl reiniciável, os timeouts relativos tendem a ser muito
   imprecisos (coarse) e podem estender indefinidamente o seu tempo de espera
   devido ao arredondamento a cada reinicialização. Especialmente se o seu relógio
   de referência for algo realmente lento, como o contador de quadros da tela
   (display frame counter). Vestindo o chapéu de advogado de especificações, isso
   não é um bug, já que os timeouts sempre podem ser estendidos -- mas os usuários
   com certeza vão odiar você se as belas animações deles começarem a gaguejar
   (stutter) devido a isso.

 * Considere descartar quaisquer ioctls de espera síncrona com timeouts e apenas
   entregue um evento assíncrono em um descritor de arquivo passível de poll
   (pollable file descriptor). Isso se encaixa muito melhor no loop principal de
   aplicações orientadas a eventos.

 * Tenha casos de teste para cenários complexos (corner-cases), especialmente se
   os valores de retorno para eventos já concluídos, esperas bem-sucedidas e
   esperas que estouraram o tempo (timed-out) são todos sãos e adequados às suas
   necessidades.


Evitando o vazamento de recursos (Leaking Resources, Not)
---------------------------------------------------------

Um driver drm completo essencialmente implementa um pequeno SO, mas especializado
para as plataformas de GPU fornecidas. Isso significa que um driver precisa
expor toneladas de handles (identificadores) para diferentes objetos e outros
recursos para o espaço de usuário. Fazer isso corretamente traz seu próprio
pequeno conjunto de armadilhas:

 * Sempre vincule o tempo de vida (lifetime) de seus recursos criados
   dinamicamente ao tempo de vida de um descritor de arquivo (file descriptor -
   fd). Considere usar um mapeamento 1:1 se o seu recurso precisar ser
   compartilhado entre processos -- a passagem de fds sobre unix domain sockets
   também simplifica o gerenciamento do tempo de vida para o espaço de usuário.

 * Sempre tenha suporte a O_CLOEXEC.

 * Certifique-se de que você tem isolamento suficiente entre os diferentes
   clientes. Por padrão, escolha um namespace privado por fd, o que força
   qualquer compartilhamento a ser feito de forma explícita. Só adote um
   namespace mais global por dispositivo se os objetos forem verdadeiramente
   únicos do dispositivo. Um contraexemplo nas interfaces de modeset do drm é
   que os objetos de modeset por dispositivo, como conectores, compartilham um
   namespace com objetos de framebuffer, que na maioria das vezes não são
   compartilhados de forma alguma. Um namespace separado, privado por padrão,
   para os framebuffers teria sido mais adequado.

 * Pense sobre os requisitos de unicidade para os handles do espaço de usuário.
   Por exemplo, para a maioria dos drivers drm, é um bug do espaço de usuário
   enviar o mesmo objeto duas vezes na mesma ioctl de envio de comando. Mas,
   se os objetos forem compartilháveis, o espaço de usuário precisa saber se
   já viu um objeto importado de outro processo ou não. Eu ainda não tentei isso
   sozinho devido à falta de uma nova classe de objetos, mas considere usar
   números de inode em seus descritores de arquivo compartilhados como
   identificadores únicos -- é assim que arquivos reais também são diferenciados.
   Infelizmente, isso requer um sistema de arquivos virtual completo no kernel.


Por último, mas não menos importante
------------------------------------

Nem todo problema precisa de uma nova ioctl:

 * Pense bem se você realmente quer uma interface privada do driver. Claro que
   é muito mais rápido aprovar uma interface privada do driver do que se envolver
   em discussões longas por uma solução mais genérica. E, ocasionalmente, criar
   uma interface privada para liderar um novo conceito é o que se exige. Mas,
   no final, assim que a interface genérica surgir, você acabará mantendo duas
   interfaces. Indefinidamente.

 * Considere outras interfaces além de ioctls. Um atributo sysfs é muito melhor
   para configurações por dispositivo ou para objetos filhos com tempos de vida
   razoavelmente estáticos (como conectores de saída no drm com todos os seus
   atributos de sobreposição de detecção). Ou talvez apenas a sua suíte de
   testes precise dessa interface e, nesse caso, o debugfs, com seu aviso de
   isenção de responsabilidade por não ter uma ABI estável, seria melhor.

Finalmente, o objetivo principal é acertar na primeira tentativa, pois se o seu
driver se provar popular e suas plataformas de hardware forem duradouras, você
ficará preso a uma determinada ioctl essencialmente para sempre. Você pode
tentar depreciar ioctls horríveis em iterações mais novas do seu hardware, mas
geralmente leva anos para conseguir isso. E depois mais anos até que o último
usuário capaz de reclamar sobre regressões desapareça também.